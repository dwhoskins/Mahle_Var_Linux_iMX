// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2019 NXP
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/virtio.h>
#include <linux/rpmsg.h>

#define MSG		"hello world!"

static int rpmsg_m4_cb(struct rpmsg_device *rpdev, void *data, int len, void *priv, u32 src)
{
	int err;
	unsigned int rpmsg_pingpong;

	/* reply */
	rpmsg_pingpong = *(unsigned int *)data;
	pr_info("get %d (src: 0x%x)\n", rpmsg_pingpong, src);

	/* pingpongs should not live forever */
	if (rpmsg_pingpong > 100) {
		dev_info(&rpdev->dev, "goodbye!\n");
		return 0;
	}
	
	rpmsg_pingpong++;
	err = rpmsg_sendto(rpdev->ept, (void *)(&rpmsg_pingpong), 4, src);

	if(err)
	{
		dev_err(&rpdev->dev, "rpmsg_send failed: %d\n", err);
	}

	return err;
}

static int rpmsg_m4_probe(struct rpmsg_device *rpdev)
{
	int err;
	unsigned int rpmsg_pingpong;

	dev_info(&rpdev->dev, "new channel: 0x%x -> 0x%x!\n", rpdev->src, rpdev->dst);

	/*
	 * send a message to our remote processor, and tell remote
	 * processor about this channel
	 */
	
	err = rpmsg_send(rpdev->ept, MSG, strlen(MSG));
	if(err)
	{
		dev_err(&rpdev->dev, "rpmsg_send failed: %d\n", err);
		return err;
	}

	rpmsg_pingpong = 0;
	err = rpmsg_sendto(rpdev->ept, (void *)(&rpmsg_pingpong), 4, rpdev->dst);
	if(err)
	{
		dev_err(&rpdev->dev, "rpmsg_send failed: %d\n", err);
		return err;
	}

	return 0;
}

static void rpmsg_m4_remove(struct rpmsg_device *rpdev)
{
	dev_info(&rpdev->dev, "rpmsg m4 driver is removed\n");
}

static struct rpmsg_device_id rpmsg_driver_m4_id_table[] = 
{
	{ .name	= "M4_VCI" },
	{ },
};

static struct rpmsg_driver rpmsg_m4_driver = 
{
	.drv.name	= KBUILD_MODNAME,
	.drv.owner	= THIS_MODULE,
	.id_table	= rpmsg_driver_m4_id_table,
	.probe		= rpmsg_m4_probe,
	.callback	= rpmsg_m4_cb,
	.remove		= rpmsg_m4_remove,
};

static int __init rpmsg_m4_init(void)
{
	return register_rpmsg_driver(&rpmsg_m4_driver);
}

static void __exit rpmsg_m4_exit(void)
{
	unregister_rpmsg_driver(&rpmsg_m4_driver);
}
module_init(rpmsg_m4_init);
module_exit(rpmsg_m4_exit);

MODULE_AUTHOR("Mahle Service Solutions");
MODULE_DESCRIPTION("iMX virtio remote processor messaging driver");
MODULE_LICENSE("GPL v2");