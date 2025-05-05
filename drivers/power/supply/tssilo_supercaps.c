// SPDX-License-Identifier: GPL-2.0
/*
 * Supercaps driver for the embeddedTS SILO controller
 * Copyright (C) 2024-2025 Technologic Systems, Inc. dba embeddedTS
 */
#define POWER_FAIL_HANDLER 1

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/init.h>

#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/power_supply.h>
#include <linux/of.h>
#include <linux/mfd/ts_supervisor.h>

#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/delay.h>

#define SILO_VERSION_REG    (SUPER_SILO_BASE+0)
#define SILO_STATUS_REG   (SUPER_SILO_BASE+1)
#define SILO_CONTROL_REG  (SUPER_SILO_BASE+2)
#define SILO_PCT_CHARGED_REG   (SUPER_SILO_BASE+4)
#define SILO_STARTUP_REQUESTED_CHG_CURRENT_MA_REG (SUPER_SILO_BASE+5)
#define SILO_REQUESTED_CHG_CURRENT_MA_REG (SUPER_SILO_BASE+6)

#define SUPER_MIN_PWR_ON_PCT (SUPER_SILO_BASE+7)

enum silo_status_bits {
	SILO_CHARGING = BIT(0),
};

enum silo_control_bits {
	SILO_CHARGE_ENABLE = BIT(0),
	SILO_PWRUP = BIT(1),
};

struct tssilo_supercaps_data {
	struct ts_supervisor *super;
	struct regmap *regmap;
	struct power_supply *psy;
	struct device *dev;
	struct gpio_desc *power_fail_gpio;
	struct work_struct power_fail_async_work;
	struct delayed_work power_fail_poll_work;
	int irq;
	struct mutex lock;
	unsigned int version;
	bool poll_due_to_power_fail;
	unsigned int prev_pct_charged;  // for use by thread
};

static int get_pct_charged(struct tssilo_supercaps_data *data);

/* vvvvv Begin sysfs (/sys/devices/power/supply) */

static ssize_t charging_show(struct device *dev,
			     struct device_attribute *attr,
			     char *buf)
{
	struct tssilo_supercaps_data *data = dev_get_drvdata(dev);
	int val;

	BUG_ON(IS_ERR(data));

	val = regmap_test_bits(data->regmap, SILO_STATUS_REG, SILO_CHARGING);
	if (val < 0) {
		dev_err(dev, "%s failed from regmap_read (rc=%x)\n", __func__, val);
		return sprintf(buf, "ERROR (rc=%d)\n", val);
	}

	return sprintf(buf, "%d\n", val);
}

#define MAX_CHARGE_CURRENT_MA 900

static int get_pct_charged(struct tssilo_supercaps_data *data)
{
	int ret;
	unsigned int val;

	ret = regmap_read(data->regmap, SILO_PCT_CHARGED_REG, &val);
	if (ret) {
		dev_err(data->dev, "%s failed from regmap_read (rc=%x)\n", __func__, ret);
		return ret;
	}
	return val;
}

static ssize_t pct_charged_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct tssilo_supercaps_data *data = dev_get_drvdata(dev);
	int ret;

	BUG_ON(IS_ERR(data));

	ret = get_pct_charged(data);
	if (ret < 0)
		return sprintf(buf, "ERROR (rc=%d)\n", ret);

	return sprintf(buf, "%d\n", ret);
}

static ssize_t charge_enabled_show(struct device *dev,
				   struct device_attribute *attr,
				   char *buf)
{
	struct tssilo_supercaps_data *data = dev_get_drvdata(dev);
	int val;

	BUG_ON(IS_ERR(data));

	val = regmap_test_bits(data->regmap, SILO_CONTROL_REG, SILO_CHARGE_ENABLE);
	if (val < 0) {
		dev_err(dev, "%s failed from regmap_read (rc=%x)\n", __func__, val);
		return sprintf(buf, "ERROR (rc=%d)\n", val);
	}

	return sprintf(buf, "%d\n", val);
}

static ssize_t charge_enabled_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	struct tssilo_supercaps_data *data = dev_get_drvdata(dev);
	int ret, val;

	BUG_ON(IS_ERR(data));

	ret = kstrtoint(buf, 10, &val);
	if (ret)
		return ret;

	if ((val != 0) && (val != 1))
		return -EINVAL;

	ret = regmap_update_bits(data->regmap, SILO_CONTROL_REG,
				 SILO_CHARGE_ENABLE, val ? SILO_CHARGE_ENABLE : 0);
	if (ret) {
		dev_err(dev, "%s: regmap_write returned %d\n", __func__, ret);
		return ret;
	}

	power_supply_changed(data->psy);

	return count;
}

static ssize_t max_charge_current_ma_default_show(struct device *dev,
						  struct device_attribute *attr,
						  char *buf)
{
	struct tssilo_supercaps_data *data = dev_get_drvdata(dev);
	int ret, val;

	BUG_ON(IS_ERR(data));

	ret = regmap_read(data->regmap, SILO_STARTUP_REQUESTED_CHG_CURRENT_MA_REG, &val);
	if (ret)
		return ret;
	return sprintf(buf, "%d\n", val);
}

static ssize_t max_charge_current_ma_default_store(struct device *dev,
						   struct device_attribute *attr,
						   const char *buf, size_t count)
{
	struct tssilo_supercaps_data *data = dev_get_drvdata(dev);
	int ret, val;

	BUG_ON(IS_ERR(data));

	ret = kstrtoint(buf, 10, &val);
	if (ret)
		return ret;

	if (val < 0 || val > MAX_CHARGE_CURRENT_MA)
		return -EINVAL;

	ret = regmap_write(data->regmap, SILO_STARTUP_REQUESTED_CHG_CURRENT_MA_REG, val);
	if (ret)
		return ret;
	power_supply_changed(data->psy);
	return count;
}

static ssize_t power_fail_show(struct device *dev,
			       struct device_attribute *attr,
			       char *buf)
{
	struct tssilo_supercaps_data *data = dev_get_drvdata(dev);
	int val;

	val = gpiod_get_value_cansleep(data->power_fail_gpio);
	return sprintf(buf, "%d\n", val);
}

static DEVICE_ATTR_RW(charge_enabled);
static DEVICE_ATTR_RW(max_charge_current_ma_default);
static DEVICE_ATTR_RO(charging);
static DEVICE_ATTR_RO(pct_charged);
static DEVICE_ATTR_RO(power_fail);

static struct attribute *tssilo_supercaps_attrs[] = {
	&dev_attr_charge_enabled.attr,
	&dev_attr_charging.attr,
	&dev_attr_max_charge_current_ma_default.attr,
	&dev_attr_pct_charged.attr,
	&dev_attr_power_fail.attr,
	NULL,
};

static const struct attribute_group tssilo_supercaps_attr_group = {
	.attrs = tssilo_supercaps_attrs,
};

/* ^^^^^ End of sysfs (/sys/devices/power/supply) */

static enum power_supply_property tssilo_supercaps_props[] = {
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT,
	POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT_MAX,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX
};

static int tssilo_supercaps_get_property(struct power_supply *psy,
					 enum power_supply_property psp,
					 union power_supply_propval *val)
{
	int ret;
	int charge_enabled;

	struct tssilo_supercaps_data *data = power_supply_get_drvdata(psy);

	BUG_ON(IS_ERR(data));

	switch (psp) {
	case POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT:
		charge_enabled = regmap_test_bits(data->regmap, SILO_CONTROL_REG, SILO_CHARGE_ENABLE);
		if (charge_enabled < 0)
			return charge_enabled;
		val->intval = (charge_enabled ? 100 : 0);
		return 0;
	case POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT_MAX:
		val->intval = 100;
		return 0;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = 1;
		return 0;
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = !gpiod_get_value_cansleep(data->power_fail_gpio);
		return 0;
	case POWER_SUPPLY_PROP_CAPACITY:
		ret = get_pct_charged(data);
		if (ret < 0)
			return ret;
		val->intval = ret;
		return 0;
	case POWER_SUPPLY_PROP_STATUS:
		charge_enabled = regmap_test_bits(data->regmap, SILO_CONTROL_REG, SILO_CHARGE_ENABLE);
		if (charge_enabled < 0)
			return charge_enabled;
		if (gpiod_get_value_cansleep(data->power_fail_gpio)) {
			val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
		} else if (charge_enabled) {
			ret = get_pct_charged(data);
			if (ret < 0)
				return ret;
			if (ret == 100) {
				val->intval = POWER_SUPPLY_STATUS_FULL;
			} else if (regmap_test_bits(data->regmap, SILO_STATUS_REG, SILO_CHARGING)) {
				val->intval = POWER_SUPPLY_STATUS_CHARGING;
			} else {
				val->intval = POWER_SUPPLY_STATUS_NOT_CHARGING;
			}
		} else if (regmap_test_bits(data->regmap, SUPER_FEATURES0, SUPER_FEAT_SILO)) {
			val->intval = POWER_SUPPLY_STATUS_NOT_CHARGING;
		} else {
			val->intval = POWER_SUPPLY_STATUS_UNKNOWN;
		}
		return 0;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
		ret = regmap_read(data->regmap, SILO_REQUESTED_CHG_CURRENT_MA_REG, &val->intval);
		if (ret < 0)
			return ret;
		return 0;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
		val->intval = MAX_CHARGE_CURRENT_MA;
		return 0;
	default:
		return -EINVAL;
	}
	return -ENODATA;
}

static int tssilo_supercaps_set_property(struct power_supply *psy,
					 enum power_supply_property psp,
					 const union power_supply_propval *val)
{
	struct tssilo_supercaps_data *data = power_supply_get_drvdata(psy);
	int ret;

	BUG_ON(IS_ERR(data));

	switch (psp) {
	case POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT:
		ret = regmap_update_bits(data->regmap, SILO_CONTROL_REG,
					 SILO_CHARGE_ENABLE,
					 val->intval ? SILO_CHARGE_ENABLE : 0);
		if (ret < 0)
			return ret;
		power_supply_changed(data->psy);
		return 0;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
		ret = regmap_write(data->regmap, SILO_REQUESTED_CHG_CURRENT_MA_REG, val->intval);
		if (ret < 0)
			return ret;
		power_supply_changed(data->psy);
		return 0;
	default:
		return -EINVAL;
	}
	return -ENODATA;
}

static const struct power_supply_desc tssilo_supercaps_desc = {
	.name = "tssilo_supercaps",
	.type = POWER_SUPPLY_TYPE_UPS,
	.properties = tssilo_supercaps_props,
	.num_properties = ARRAY_SIZE(tssilo_supercaps_props),
	.get_property = tssilo_supercaps_get_property,
	.set_property = tssilo_supercaps_set_property,
};

#ifdef POWER_FAIL_HANDLER
/*
 * irq_handler goes here (when ready to move past the GPIO)
 */

static irqreturn_t power_fail_irq_handler(int irq, void *data)
{
	struct tssilo_supercaps_data *silo_data = data;
	schedule_work(&silo_data->power_fail_async_work);
	return IRQ_HANDLED;
}

static void power_fail_async_worker(struct work_struct *work)
{
	struct tssilo_supercaps_data *silo_data = container_of(work, struct tssilo_supercaps_data, power_fail_async_work);
	bool active = gpiod_get_value_cansleep(silo_data->power_fail_gpio);

	dev_info(silo_data->dev, "%s: power_fail = %d\n", __func__, active);

	mutex_lock(&silo_data->lock);

	if (active && !silo_data->poll_due_to_power_fail) {
		silo_data->poll_due_to_power_fail = true;
		schedule_delayed_work(&silo_data->power_fail_poll_work, 0);
	} else if (!active && silo_data->poll_due_to_power_fail) {
		silo_data->poll_due_to_power_fail = false;
		cancel_delayed_work_sync(&silo_data->power_fail_poll_work);
		power_supply_changed(silo_data->psy);
	}

	mutex_unlock(&silo_data->lock);
}

static void power_fail_poll_worker(struct work_struct *work)
{
	struct tssilo_supercaps_data *silo_data = container_of(to_delayed_work(work), struct tssilo_supercaps_data, power_fail_poll_work);
	bool active = gpiod_get_value_cansleep(silo_data->power_fail_gpio);
	int ret;

	dev_info(silo_data->dev, "%s: power_fail = %d\n", __func__, active);

	mutex_lock(&silo_data->lock);

	if (!silo_data->poll_due_to_power_fail) {
		mutex_unlock(&silo_data->lock);
		dev_info(silo_data->dev, "%s: polling for some other reason than power fail.\n", __func__);
		power_supply_changed(silo_data->psy);
		return;
	}

	ret = get_pct_charged(silo_data);
	if (ret < 0)
		goto early_poll_return;

	if (ret != silo_data->prev_pct_charged) {
		silo_data->prev_pct_charged = ret;
		power_supply_changed(silo_data->psy);
	}

early_poll_return:

	/*
	 * Continue as long as POWER_FAIL is asserted
	 */
	schedule_delayed_work(&silo_data->power_fail_poll_work, HZ);
	mutex_unlock(&silo_data->lock);
}
#endif

static int ts_silo_probe(struct platform_device *pdev)
{
	struct ts_supervisor *wizard = dev_get_drvdata(pdev->dev.parent);
	struct device *dev = &pdev->dev;
	struct tssilo_supercaps_data *data;
	struct power_supply_config psy_cfg = {};
	int ret;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	wizard->silo_pdev = pdev;
	data->regmap = wizard->regmap;
	platform_set_drvdata(pdev, data);

	data->power_fail_gpio = devm_gpiod_get(dev, "power-fail", GPIOD_IN);
	if (IS_ERR(data->power_fail_gpio)) {
		ret = PTR_ERR(data->power_fail_gpio);
		dev_err(dev, "%s could not find the power-fail gpio (rc=%d)", __func__, ret);
		return ret;
	}
#ifdef POWER_FAIL_HANDLER
	data->irq = gpiod_to_irq(data->power_fail_gpio);
	if (data->irq < 0)
		return data->irq;
#endif

	psy_cfg.drv_data = data;
	data->psy = devm_power_supply_register(dev, &tssilo_supercaps_desc, &psy_cfg);
	if (IS_ERR(data->psy)) {
		dev_err(dev, "%s: devm_power_supply_register failed (rc=%pe)", __func__, data->psy);
		return PTR_ERR(data->psy);
	}

	ret = sysfs_create_group(&dev->kobj, &tssilo_supercaps_attr_group);
	if (ret) {
		dev_err(dev, "%s: sysfs_create_group failed (rc=%d)\n", __func__, ret);
		return ret;
	}

#ifdef POWER_FAIL_HANDLER
	INIT_WORK(&data->power_fail_async_work, power_fail_async_worker);
	INIT_DELAYED_WORK(&data->power_fail_poll_work, power_fail_poll_worker);

	ret = devm_request_irq(dev, data->irq, power_fail_irq_handler,
			       IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
			       "ups-power_fail", data);
	if (ret)
		return ret;
#endif

	/* Show how probe went; this will be reserved and normally 0, except during development changes */
	ret = regmap_read(data->regmap, SILO_VERSION_REG, &data->version);
	if (ret < 0)
		return ret;
	dev_info(dev, "TS-SILO version %d\n", data->version);

	return 0;
}

static const struct of_device_id tssilo_supercaps_of_match[] = {
	{ .compatible = "technologic,tssilo-power-supply", },
	{}
};
MODULE_DEVICE_TABLE(of, tssilo_supercaps_of_match);

static struct platform_driver tssilo_supercaps_driver = {
	.driver = {
		.name = "tssilo_supercaps",
		.of_match_table = tssilo_supercaps_of_match,
	},
	.probe = ts_silo_probe,
};

module_platform_driver(tssilo_supercaps_driver);

MODULE_DESCRIPTION("embeddedTS SILO supercaps driver");
MODULE_AUTHOR("Lionel D. Hummel <lionel@embeddedTS.com>");
MODULE_LICENSE("GPL");
