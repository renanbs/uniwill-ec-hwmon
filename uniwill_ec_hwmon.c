// SPDX-License-Identifier: GPL-2.0-only
/*
 * uniwill_ec_hwmon - read-only hwmon driver for Uniwill/Tongfang ECs
 *
 * Some Uniwill-based notebooks (Avell/Tongfang rebrands included) do not
 * expose the INOU0000 ACPI device required by the in-tree uniwill-laptop
 * driver, but do expose the ACPI-WMI "AcpiTest_MULong" method described in
 * Documentation/wmi/devices/uniwill-laptop.rst.
 *
 * This driver only reads EC registers; it never writes to the EC.
 *
 * Register addresses and encoding match drivers/platform/x86/uniwill:
 *   temperature: one byte, degrees Celsius
 *   fan tach:    two bytes, big endian, RPM
 *   pwm:         one byte, 0..255
 */

#include <linux/acpi.h>
#include <linux/delay.h>
#include <linux/hwmon.h>
#include <linux/mutex.h>
#include <linux/module.h>
#include <linux/wmi.h>

#define UW_WMI_GUID		"ABBC0F6F-8EA1-11D1-00A0-C90629100000"
#define UW_WMI_INSTANCE		0
#define UW_WMI_METHOD		4
#define UW_WMI_INPUT_LEN	40
#define UW_EC_READ_OP		0x01
#define UW_EC_FAIL_MARKER	0xfe

#define UW_REG_CPU_TEMP		0x043e
#define UW_REG_GPU_TEMP		0x044f
#define UW_REG_FAN1_RPM_MSB	0x0464
#define UW_REG_FAN1_RPM_LSB	0x0465
#define UW_REG_FAN2_RPM_MSB	0x046c
#define UW_REG_FAN2_RPM_LSB	0x046d
#define UW_REG_PWM1		0x075b
#define UW_REG_PWM2		0x075c

#define UW_EC_RETRIES		3
#define UW_EC_RETRY_DELAY_US	1000

struct uw_ec {
	struct mutex lock;	/* serializes EC transactions */
};

static int uw_ec_read_byte(struct uw_ec *ec, u16 addr, u8 *value)
{
	u32 args[UW_WMI_INPUT_LEN / sizeof(u32)] = { };
	u8 *bytes = (u8 *)args;
	const struct acpi_buffer in = {
		.length = sizeof(args),
		.pointer = args,
	};
	int ret = -EIO;
	int attempt;

	bytes[0] = addr & 0xff;
	bytes[1] = addr >> 8;
	bytes[5] = UW_EC_READ_OP;

	mutex_lock(&ec->lock);
	for (attempt = 0; attempt < UW_EC_RETRIES; attempt++) {
		struct acpi_buffer out = { ACPI_ALLOCATE_BUFFER, NULL };
		union acpi_object *obj;
		acpi_status status;

		status = wmi_evaluate_method(UW_WMI_GUID, UW_WMI_INSTANCE,
					     UW_WMI_METHOD, &in, &out);
		obj = out.pointer;
		if (ACPI_SUCCESS(status) && obj &&
		    obj->type == ACPI_TYPE_BUFFER && obj->buffer.length >= 1 &&
		    !(obj->buffer.length >= 4 &&
		      obj->buffer.pointer[0] == UW_EC_FAIL_MARKER &&
		      obj->buffer.pointer[1] == UW_EC_FAIL_MARKER &&
		      obj->buffer.pointer[2] == UW_EC_FAIL_MARKER &&
		      obj->buffer.pointer[3] == UW_EC_FAIL_MARKER)) {
			*value = obj->buffer.pointer[0];
			ret = 0;
		}
		kfree(obj);
		if (!ret)
			break;
		usleep_range(UW_EC_RETRY_DELAY_US, 2 * UW_EC_RETRY_DELAY_US);
	}
	mutex_unlock(&ec->lock);

	return ret;
}

static int uw_read_fan(struct uw_ec *ec, int channel, long *val)
{
	u16 msb_addr = channel ? UW_REG_FAN2_RPM_MSB : UW_REG_FAN1_RPM_MSB;
	u8 msb, lsb;
	int ret;

	ret = uw_ec_read_byte(ec, msb_addr, &msb);
	if (ret)
		return ret;
	ret = uw_ec_read_byte(ec, msb_addr + 1, &lsb);
	if (ret)
		return ret;

	*val = (msb << 8) | lsb;
	return 0;
}

static umode_t uw_is_visible(const void *drvdata, enum hwmon_sensor_types type,
			     u32 attr, int channel)
{
	if (channel > 1)
		return 0;

	switch (type) {
	case hwmon_temp:
	case hwmon_fan:
	case hwmon_pwm:
		return 0444;
	default:
		return 0;
	}
}

static int uw_read(struct device *dev, enum hwmon_sensor_types type, u32 attr,
		   int channel, long *val)
{
	struct uw_ec *ec = dev_get_drvdata(dev);
	u8 byte;
	int ret;

	switch (type) {
	case hwmon_temp:
		ret = uw_ec_read_byte(ec, channel ? UW_REG_GPU_TEMP : UW_REG_CPU_TEMP,
				      &byte);
		if (ret)
			return ret;
		*val = byte * 1000L;
		return 0;
	case hwmon_fan:
		return uw_read_fan(ec, channel, val);
	case hwmon_pwm:
		ret = uw_ec_read_byte(ec, channel ? UW_REG_PWM2 : UW_REG_PWM1,
				      &byte);
		if (ret)
			return ret;
		*val = byte;
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static int uw_read_string(struct device *dev, enum hwmon_sensor_types type,
			  u32 attr, int channel, const char **str)
{
	static const char * const temp_labels[] = { "CPU", "GPU" };
	static const char * const fan_labels[] = { "CPU fan", "GPU fan" };

	switch (type) {
	case hwmon_temp:
		*str = temp_labels[channel];
		return 0;
	case hwmon_fan:
		*str = fan_labels[channel];
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static const struct hwmon_ops uw_hwmon_ops = {
	.is_visible = uw_is_visible,
	.read = uw_read,
	.read_string = uw_read_string,
};

static const struct hwmon_channel_info * const uw_hwmon_info[] = {
	HWMON_CHANNEL_INFO(temp,
			   HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL),
	HWMON_CHANNEL_INFO(fan,
			   HWMON_F_INPUT | HWMON_F_LABEL,
			   HWMON_F_INPUT | HWMON_F_LABEL),
	HWMON_CHANNEL_INFO(pwm,
			   HWMON_PWM_INPUT,
			   HWMON_PWM_INPUT),
	NULL
};

static const struct hwmon_chip_info uw_chip_info = {
	.ops = &uw_hwmon_ops,
	.info = uw_hwmon_info,
};

static int uw_probe(struct wmi_device *wdev, const void *context)
{
	struct uw_ec *ec;
	struct device *hdev;

	ec = devm_kzalloc(&wdev->dev, sizeof(*ec), GFP_KERNEL);
	if (!ec)
		return -ENOMEM;

	mutex_init(&ec->lock);
	dev_set_drvdata(&wdev->dev, ec);

	hdev = devm_hwmon_device_register_with_info(&wdev->dev, "uniwill_ec",
						    ec, &uw_chip_info, NULL);
	return PTR_ERR_OR_ZERO(hdev);
}

static const struct wmi_device_id uw_wmi_ids[] = {
	{ .guid_string = UW_WMI_GUID },
	{ }
};
MODULE_DEVICE_TABLE(wmi, uw_wmi_ids);

static struct wmi_driver uw_wmi_driver = {
	.driver = {
		.name = "uniwill_ec_hwmon",
	},
	.id_table = uw_wmi_ids,
	.probe = uw_probe,
};
module_wmi_driver(uw_wmi_driver);

MODULE_AUTHOR("Renan");
MODULE_DESCRIPTION("Read-only hwmon driver for Uniwill/Tongfang EC via ACPI-WMI");
MODULE_LICENSE("GPL");
