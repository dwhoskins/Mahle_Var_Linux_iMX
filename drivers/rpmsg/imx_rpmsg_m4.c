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
#include <linux/list.h>
#include <linux/semaphore.h>

#define MSG									"Connect"
#define RPMSG_M4_NUM_DEVICES				1
#define RX_MSG_BUFFER_COUNT					10
#define RX_MSG_BUFFER_DATA_SZ				512

struct listItemRxMsg
{
	int 				iRxDataLen;
	char * 				pcData;
	struct list_head 	list;
};

struct t_stRPMsg_M4
{
	struct rpmsg_device *	rpdev;
	struct cdev 			cdev;
	struct device 			dev;
	struct semaphore 		RxCountingSema;
	spinlock_t				lockFreeBuffers;
	spinlock_t				lockActiveBuffers;
	struct list_head 		freeBuffers;
	struct list_head 		activeBuffers;
	struct listItemRxMsg * 	alistItemRxMsgs[RX_MSG_BUFFER_COUNT];
};




static int device_open(struct inode *, struct file *);
static int device_release(struct inode *, struct file *);
static ssize_t device_read(struct file *, char *, size_t, loff_t *);
static ssize_t device_write(struct file *, const char *, size_t, loff_t *);


int majorNum;
int minorNum = 1;
dev_t firstDevNo = 0;  // Major device number
struct class * rpmsg_class;  // class_create will set this


char kernelbuff[512];


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
	return 0;
}

//--------------------------------------------------------------------------------------------------------
static int device_release(struct inode *inode, struct file *fp)
{
	struct t_stRPMsg_M4 * pstRPMsg_M4 = fp->private_data;
	
	return 0;
}

//--------------------------------------------------------------------------------------------------------
static ssize_t device_read(struct file *fp, char *ch, size_t sz, loff_t *lofft)
{
	struct t_stRPMsg_M4 * pstRPMsg_M4 = fp->private_data;
	struct listItemRxMsg * plistItemRxMsg = NULL;

	if(down_interruptible(&pstRPMsg_M4->RxCountingSema) == 0)
	{
		spin_lock(&pstRPMsg_M4->lockActiveBuffers);
		if(list_empty(&pstRPMsg_M4->activeBuffers) == false)
		{
			plistItemRxMsg = list_first_entry(&pstRPMsg_M4->activeBuffers, struct listItemRxMsg, list);
			spin_unlock(&pstRPMsg_M4->lockActiveBuffers);

			//TODO
			//Should check sz before copying
			copy_to_user(ch, plistItemRxMsg->pcData, plistItemRxMsg->iRxDataLen);

			spin_lock(&pstRPMsg_M4->lockFreeBuffers);
			list_move_tail(&plistItemRxMsg->list, &pstRPMsg_M4->freeBuffers);
			spin_unlock(&pstRPMsg_M4->lockFreeBuffers);

			return plistItemRxMsg->iRxDataLen;
		}
		else
		{
			spin_unlock(&pstRPMsg_M4->lockActiveBuffers);
			pr_err("device_read: Semphore signaled, but no buffer\n");
		}
	}
	else
	{
		pr_err("device_read: Semphore returned non-sucessful \n");
	}
	return 0;
}

//--------------------------------------------------------------------------------------------------------
static ssize_t device_write(struct file *fp, const char *ch, size_t sz, loff_t * offset)
{
	int err;
	struct t_stRPMsg_M4 * pstRPMsg_M4 = fp->private_data;
		

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
	struct t_stRPMsg_M4 * pstRPMsg_M4 = dev_get_drvdata(&rpdev->dev);
	struct listItemRxMsg * plistItemRxMsg = NULL;
	int iLenToCopy;

	spin_lock(&pstRPMsg_M4->lockFreeBuffers);
	if(list_empty(&pstRPMsg_M4->freeBuffers) == true)
	{
		spin_unlock(&pstRPMsg_M4->lockFreeBuffers);
		pr_err("rpmsg_m4: no free Rx Buffers\n");
		return 0;
	}
	else
	{

		//Get free list item from free list
		plistItemRxMsg = list_first_entry(&pstRPMsg_M4->freeBuffers, struct listItemRxMsg, list);
		spin_unlock(&pstRPMsg_M4->lockFreeBuffers);

		if(len > RX_MSG_BUFFER_DATA_SZ)
		{
			iLenToCopy = RX_MSG_BUFFER_DATA_SZ;
		}
		else
		{
			iLenToCopy = len;
		}

		//Copy message into list item
		memcpy(plistItemRxMsg->pcData, data, iLenToCopy);
		plistItemRxMsg->iRxDataLen = iLenToCopy;


		//Add list item to active list
		spin_lock(&pstRPMsg_M4->lockActiveBuffers);
		list_move_tail(&plistItemRxMsg->list, &pstRPMsg_M4->activeBuffers);
		spin_unlock(&pstRPMsg_M4->lockActiveBuffers);

		up(&pstRPMsg_M4->RxCountingSema);
	}

	return 0;
}

static int rpmsg_m4_init_rx_buffers(struct t_stRPMsg_M4 * pstRPMsg_M4)
{
	int iListItemInc = 0;

	INIT_LIST_HEAD(&pstRPMsg_M4->freeBuffers);
	INIT_LIST_HEAD(&pstRPMsg_M4->activeBuffers);

	for(iListItemInc = 0 ; iListItemInc < RX_MSG_BUFFER_COUNT; iListItemInc ++)
	{
		pstRPMsg_M4->alistItemRxMsgs[iListItemInc] = kmalloc(sizeof(struct listItemRxMsg), GFP_KERNEL);
		INIT_LIST_HEAD(&pstRPMsg_M4->alistItemRxMsgs[iListItemInc]->list);
		pstRPMsg_M4->alistItemRxMsgs[iListItemInc]->pcData = kzalloc(RX_MSG_BUFFER_DATA_SZ, GFP_KERNEL);
		list_add(&pstRPMsg_M4->alistItemRxMsgs[iListItemInc]->list, &pstRPMsg_M4->freeBuffers);
	}

	return 0;
}

static int rpmsg_m4_release_rx_buffers(struct t_stRPMsg_M4 * pstRPMsg_M4)
{
	int iListItemInc = 0;

	for(iListItemInc = 0 ; iListItemInc < RX_MSG_BUFFER_COUNT; iListItemInc ++)
	{
		kfree(pstRPMsg_M4->alistItemRxMsgs[iListItemInc]->pcData);
		kfree(pstRPMsg_M4->alistItemRxMsgs[iListItemInc]);
	}
	return 0;
}

//--------------------------------------------------------------------------------------------------------
static void rpmsg_m4_release_device(struct device *dev)
{
	struct t_stRPMsg_M4 * pstRPMsg_M4 = container_of(dev, struct t_stRPMsg_M4, dev);

	dev_info(&pstRPMsg_M4->rpdev->dev, "rpmsg_m4 release device\n");

	//Release list items and data buffers
	rpmsg_m4_release_rx_buffers(pstRPMsg_M4);
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

	//Init list heads and list items
	rpmsg_m4_init_rx_buffers(pstRPMsg_M4);

	spin_lock_init(&pstRPMsg_M4->lockFreeBuffers);

	spin_lock_init(&pstRPMsg_M4->lockActiveBuffers);

	sema_init(&pstRPMsg_M4->RxCountingSema, 0);

	ret = device_register(pDev);
	if(ret != 0)
	{
		dev_err(&rpdev->dev, "device_register failed: %d\n", ret);
		rpmsg_m4_release_rx_buffers(pstRPMsg_M4);
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
