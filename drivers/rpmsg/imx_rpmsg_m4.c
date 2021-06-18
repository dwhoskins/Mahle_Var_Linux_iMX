// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2019 NXP
 */

#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/virtio.h>
#include <linux/slab.h>
#include <linux/rpmsg.h>

#define MSG									"Connect"
#define RPMSG_M4_NUM_DEVICES				1



struct t_stRPMsg_M4
{
	struct rpmsg_device *	rpdev;
	struct cdev 			cdev;
	struct device 			dev;
};








static int device_open(struct inode *, struct file *);
static int device_release(struct inode *, struct file *);
static ssize_t device_read(struct file *, char *, size_t, loff_t *);
static ssize_t device_write(struct file *, const char *, size_t, loff_t *);


int majorNum;
int minorNum = 1;
dev_t firstDevNo = 0;  // Major device number
struct class * rpmsg_class;  // class_create will set this


char kernelbuff[1024];


struct file_operations fops = 
{
.owner	= THIS_MODULE,
.read = device_read,
.write = device_write,
.open = device_open,
.release = device_release
};



//--------------------------------------------------------------------------------------------------------
int device_open(struct inode *inode, struct file *fp)
{
	struct t_stRPMsg_M4 * pstRPMsg_M4;

	//Get our main driver struct by using the below macro
	pstRPMsg_M4 = container_of(inode->i_cdev, struct t_stRPMsg_M4, cdev);

	//Set our main driver struct as files private data
	//This will allow the use of it in later callbacks
	fp->private_data = pstRPMsg_M4; /* for other methods */

	printk("device_open called");
	return 0;
}

//--------------------------------------------------------------------------------------------------------
static int device_release(struct inode *inode, struct file *fp)
{
	struct t_stRPMsg_M4 * pstRPMsg_M4 = fp->private_data;
	
	printk("device_release called");
	return 0;
}

//--------------------------------------------------------------------------------------------------------
static ssize_t device_read(struct file *fp, char *ch, size_t sz, loff_t *lofft)
{
	struct t_stRPMsg_M4 * pstRPMsg_M4 = fp->private_data;
	
	printk("device_read called");      
	copy_to_user(ch, kernelbuff, 1024);
	return sz;
}

//--------------------------------------------------------------------------------------------------------
static ssize_t device_write(struct file *fp, const char *ch, size_t sz, loff_t * offset)
{
	int err;
	struct t_stRPMsg_M4 * pstRPMsg_M4 = fp->private_data;

	printk("device_write called");
		

	copy_from_user(kernelbuff, ch, sz);
	*offset+=sz;	
	
	err = rpmsg_send(pstRPMsg_M4->rpdev->ept, kernelbuff, sz);
	if(err)
	{
		dev_err(&pstRPMsg_M4->rpdev->dev, "rpmsg_send failed: %d\n", err);
		return err;
	}

	return sz;
}



//--------------------------------------------------------------------------------------------------------
static int rpmsg_m4_cb(struct rpmsg_device *rpdev, void *data, int len, void *priv, u32 src)
{
	
	return 0;
}

//--------------------------------------------------------------------------------------------------------
static void rpmsg_m4_release_device(struct device *dev)
{
	struct t_stRPMsg_M4 * pstRPMsg_M4 = container_of(dev, struct t_stRPMsg_M4, dev);

	dev_info(&pstRPMsg_M4->rpdev->dev, "rpmsg_m4 release device\n");

	//Delete char device
	cdev_del(&pstRPMsg_M4->cdev);
	//Free our driver structure
	kfree(pstRPMsg_M4);
}

//--------------------------------------------------------------------------------------------------------
static int rpmsg_m4_probe(struct rpmsg_device *rpdev)
{
	int ret;
	struct t_stRPMsg_M4 * pstRPMsg_M4;
	struct device * pDev;

	dev_info(&rpdev->dev, "rpmsg_m4 probe\n");

	dev_info(&rpdev->dev, "new channel: 0x%x -> 0x%x!\n", rpdev->src, rpdev->dst);

	//Notify M4 that driver is loading
	ret = rpmsg_send(rpdev->ept, MSG, strlen(MSG));
	if(ret)
	{
		dev_err(&rpdev->dev, "rpmsg_send failed: %d\n", ret);
		return ret;
	}

	pstRPMsg_M4 = kzalloc(sizeof(*pstRPMsg_M4), GFP_KERNEL);
	if(!pstRPMsg_M4)
	{
		return -ENOMEM;
	}

	//Set rpmsg_device pointer
	pstRPMsg_M4->rpdev = rpdev;

	//Init and add char device
	cdev_init(&pstRPMsg_M4->cdev, &fops);
	pstRPMsg_M4->cdev.owner = THIS_MODULE;
	ret = cdev_add(&pstRPMsg_M4->cdev, firstDevNo, RPMSG_M4_NUM_DEVICES);


	pDev = &pstRPMsg_M4->dev;

	pDev->class = rpmsg_class;
	pDev->parent = &rpdev->dev;
	pDev->devt = firstDevNo;
	dev_set_name(pDev, "rpmsg_m4");
	pDev->release = rpmsg_m4_release_device;


	ret = device_register(pDev);
	if(ret != 0)
	{
		dev_err(&rpdev->dev, "device_register failed: %d\n", ret);
		put_device(pDev);
		kfree(pstRPMsg_M4);
		return ret;
	}


	//Set data to be able to get main driver structure in rpmsg callbacks
	dev_set_drvdata(&rpdev->dev, pstRPMsg_M4);

	return ret;
}

//--------------------------------------------------------------------------------------------------------
static void rpmsg_m4_remove(struct rpmsg_device *rpdev)
{
	struct t_stRPMsg_M4 * pstRPMsg_M4 = dev_get_drvdata(&rpdev->dev);
	dev_info(&rpdev->dev, "rpmsg_m4 remove\n");

	device_unregister(&pstRPMsg_M4->dev);
}

//--------------------------------------------------------------------------------------------------------
static struct rpmsg_device_id rpmsg_driver_m4_id_table[] =
{
	{ .name	= "M4_VCI" },
	{ },
};

//--------------------------------------------------------------------------------------------------------
static struct rpmsg_driver rpmsg_m4_driver =
{
	.drv.name	= KBUILD_MODNAME,
	.drv.owner	= THIS_MODULE,
	.id_table	= rpmsg_driver_m4_id_table,
	.probe		= rpmsg_m4_probe,
	.callback	= rpmsg_m4_cb,
	.remove		= rpmsg_m4_remove,
};

//--------------------------------------------------------------------------------------------------------
static int __init rpmsg_m4_init(void)
{
	int ret;

	//Dynamic allocation of device major number
	ret = alloc_chrdev_region(&firstDevNo, 0, RPMSG_M4_NUM_DEVICES, "rpmsg_m4");
	if(ret < 0)
	{
		pr_err("rpmsg_m4: failed to allocate char dev region\n");
		return ret;
	}					

	// Create /sys/class/rpmsg_m4 in preparation of creating /dev/M4_VCI
	rpmsg_class = class_create(THIS_MODULE, "rpmsg_m4");
	if(IS_ERR(rpmsg_class))
	{
		pr_err("Failed to create rpmsg_m4 class\n");
		unregister_chrdev_region(firstDevNo, RPMSG_M4_NUM_DEVICES);
		return PTR_ERR(rpmsg_class);
	}					
	
	//Register RPMsg driver
	ret = register_rpmsg_driver(&rpmsg_m4_driver);
	if(ret < 0)
	{
		pr_err("rpmsg_m4: Failed to register rpmsg driver\n");
		class_destroy(rpmsg_class);
		unregister_chrdev_region(firstDevNo, RPMSG_M4_NUM_DEVICES);
		return ret;
	}

	return ret;
}

//--------------------------------------------------------------------------------------------------------
static void __exit rpmsg_m4_exit(void)
{
	//Unregister RPMsg Driver
	unregister_rpmsg_driver(&rpmsg_m4_driver);

	//Remove class /sys/class/M4_VCI
	class_destroy(rpmsg_class);

	//Unregister Char region
	unregister_chrdev_region(firstDevNo, RPMSG_M4_NUM_DEVICES);
}


module_init(rpmsg_m4_init);
module_exit(rpmsg_m4_exit);

MODULE_AUTHOR("Mahle Service Solutions");
MODULE_DESCRIPTION("iMX M4 RPMsg Driver");
MODULE_LICENSE("GPL v2");
