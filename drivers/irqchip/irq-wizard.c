// SPDX-License-Identifier: GPL-2.0-only
/*
 *  embeddedTS Wizard MFD I2C controller
 */

#include <linux/irq.h>
#include <linux/irqchip.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/irqdomain.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/mfd/ts_supervisor.h>

#define MAX_IRQS	16

struct wizard_irq_data {
	struct regmap		*regmap;
	struct device		*dev;
	int			parent_irq;
	struct irq_domain	*domain;
	u32			base;
};

static int wizard_read(struct wizard_irq_data *data, int addr, int *value)
{
	return regmap_read(data->regmap, data->base + addr, value);
}

static int wizard_write(struct wizard_irq_data *data, int addr, int value)
{
	return regmap_write(data->regmap, data->base + addr, value);
}

static irqreturn_t wizard_irq_handler(int irq, void *devid)
{
	struct wizard_irq_data *data = devid;
	unsigned long status;
	int i;
	bool ret;

	ret = wizard_read(data, IRQ_STATUS, (u32 *)&status);
	if (ret) {
		ret = 0;

		for_each_set_bit(i, &status, MAX_IRQS) {
			generic_handle_domain_irq(data->domain, i);
			ret = 1;
		}

		/* Ack processed IRQs */
		wizard_write(data, IRQ_ACK, status);
	}

	return IRQ_RETVAL(ret);
}

static void wizard_irq_mask(struct irq_data *d)
{
	struct wizard_irq_data *data = irq_data_get_irq_chip_data(d);
	u16 reg = BIT(d->hwirq);

	wizard_write(data, IRQ_MASK_SET, reg);
}

static void wizard_irq_unmask(struct irq_data *d)
{
	struct wizard_irq_data *data = irq_data_get_irq_chip_data(d);
	u16 reg = BIT(d->hwirq);

	wizard_write(data, IRQ_MASK_CLR, reg);
}

static const struct irq_chip wizard_chip = {
	.irq_mask = wizard_irq_mask,
	.irq_unmask = wizard_irq_unmask,
};

static int wizard_irqdomain_map(struct irq_domain *d, unsigned int irq,
				 irq_hw_number_t hwirq)
{
	struct wizard_irq_data *data = d->host_data;

	irq_set_chip_and_handler(irq, &wizard_chip, handle_level_irq);
	irq_set_chip_data(irq, data);
	irq_set_noprobe(irq);

	return 0;
}

static const struct irq_domain_ops wizard_ic_ops = {
	.map = wizard_irqdomain_map,
	.xlate = irq_domain_xlate_onecell,
};

static int irq_wizard_probe(struct platform_device *pdev)
{
	struct ts_supervisor *super = dev_get_drvdata(pdev->dev.parent);
	struct device *dev = &pdev->dev;
	struct wizard_irq_data *data;
	struct device_node *node = dev->of_node;
	int ret;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->regmap = super->regmap;
	data->dev = dev;
	data->base = WIZARD_IRQCHIP_BASE;

	data->parent_irq = platform_get_irq(pdev, 0);
	if (data->parent_irq < 0)
		return data->parent_irq;

	wizard_write(data, IRQ_MASK_SET, 0xFFFF);

	data->domain = irq_domain_add_linear(node, MAX_IRQS, &wizard_ic_ops, data);
	if (!data->domain) {
		dev_err(dev, "cannot add IRQ domain\n");
		return -ENOMEM;
	}

	ret = devm_request_threaded_irq(dev, data->parent_irq,
					NULL, wizard_irq_handler,
					IRQF_ONESHOT,
					dev_name(dev), data);
	if (ret)
		goto out_domain_remove;

	return 0;

out_domain_remove:
	irq_domain_remove(data->domain);
	irq_dispose_mapping(data->parent_irq);

	return ret;
}

static const struct of_device_id wizard_of_match[] = {
	{ .compatible = "technologic,wizard-irq", },
	{ }
};
MODULE_DEVICE_TABLE(of, tsadc_of_match);

static struct platform_driver tsadc_driver = {
	.driver = {
		.name   = "wizard-irq",
		.of_match_table = wizard_of_match,
	},
	.probe	= irq_wizard_probe,
};
module_platform_driver(tsadc_driver);

MODULE_DESCRIPTION("embeddedTS wizard IRQ controller");
MODULE_AUTHOR("Mark Featherston <mark@embeddedts.com>");
MODULE_LICENSE("GPL");
