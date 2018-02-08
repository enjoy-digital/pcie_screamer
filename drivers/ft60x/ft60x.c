/*
 * USB FT60x driver - 2.2
 *
 * Copyright (C) 2018 ramtin@lambdaconcept.com
 *                    po@lambdaconcept.com
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation, version 2.
 *
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/kref.h>
#include <linux/uaccess.h>
#include <linux/usb.h>
#include <linux/mutex.h>
#include <linux/poll.h>
#include <linux/jiffies.h>
#include <linux/timer.h>

/* Define these values to match your devices */
#define USB_FT60X_VENDOR_ID	0x0403
#define USB_FT60X_PRODUCT_ID	0x601f

/* table of devices that work with this driver */
static const struct usb_device_id ft60x_table[] = {
	{USB_DEVICE(USB_FT60X_VENDOR_ID, USB_FT60X_PRODUCT_ID)},
	{}			/* Terminating entry */
};

MODULE_DEVICE_TABLE(usb, ft60x_table);

/* Get a minor  */
#ifdef CONFIG_USB_DYNAMIC_MINORS
#define USB_FT60X_MINOR_BASE	0
#else
#define USB_FT60X_MINOR_BASE	192
#endif

/* our private defines. if this grows any larger, use your own .h file */
#define MAX_TRANSFER		2048

#define WRITES_IN_FLIGHT	8
/* arbitrarily chosen */

#define FT60X_MAX_IF		5

struct ft60x_config {
	/* Device Descriptor */
	u16 	VendorID;
	u16 	ProductID;
	/* String Descriptors */
	u8 	StringDescriptors[128];
	/* Configuration Descriptor */
	u8 	Reserved;
	u8 	PowerAttributes;
	u16 	PowerConsumption;
	/* Data Transfer Configuration */
	u8 	Reserved2;
	u8 	FIFOClock;
	u8 	FIFOMode;
	u8 	ChannelConfig;
	/* Optional Feature Support */
	u16 	OptionalFeatureSupport;
	u8 	BatteryChargingGPIOConfig;
	u8 	FlashEEPROMDetection;	/* Read-only */
	/* MSIO and GPIO Configuration */
	u32 	MSIO_Control;
	u32 	GPIO_Control;
} __attribute__ ((packed));

/* Structure to hold all of our device specific stuff */
struct usb_ft60x {
	struct ft60x_device 	*ft;

	struct usb_device 	*udev;			/* the usb device for this device */
	struct usb_interface 	*interface;		/* the interface for this device */
	struct semaphore 	limit_sem;		/* limiting the number of writes in progress */
	struct usb_anchor 	submitted;		/* in case we need to retract our submissions */

	struct urb 		*int_in_urb;		/* the urb to read data with */
	size_t 			int_in_size;		/* the size of the receive buffer */
	signed char 		*int_in_buffer;		/* the buffer to receive int data */
	dma_addr_t 		int_in_data_dma;	/* the dma buffer to receive int data */
	wait_queue_head_t 	int_in_wait;		/* to wait for an ongoing read */

	bool 			got_int;		/* got interrupted */
	bool 			busy_write;		/* for the writing poll */
	bool 			done_reading;

	struct urb 		*bulk_in_urb;		/* the urb to read data with */
	size_t 			bulk_in_size;		/* the size of the receive buffer */
	size_t 			bulk_in_filled;		/* number of bytes in the buffer */
	size_t 			bulk_in_copied;		/* already copied to user space */
	unsigned char 		*bulk_in_buffer;	/* the buffer to receive data */
	wait_queue_head_t 	bulk_in_wait;		/* to wait for an ongoing read */
	wait_queue_head_t 	bulk_out_wait;		/* to wait for an ongoing write */
	bool 			ongoing_read;		/* a read is going on */

	int 			len_to_read;
	bool 			sent_cmd_read;
	bool 			waiting_int;

	__u8 			bulk_in_endpointAddr;	/* the address of the bulk in endpoint */
	__u8 			bulk_out_endpointAddr;	/* the address of the bulk out endpoint */
	__u8 			int_in_endpointAddr;	/* the address of the int in endpoint */
	int 			errors;			/* the last request tanked */
	spinlock_t 		err_lock;		/* lock for errors */
	struct kref 		kref;
	struct mutex 		io_mutex;		/* synchronize I/O with disconnect */
};
#define to_ft60x_dev(d) container_of(d, struct usb_ft60x, kref)

struct ft60x_ctrlreq {
	u32 idx;
	u8 pipe;
	u8 cmd;
	u8 unk1;
	u8 unk2;
	u32 len;
	u32 unk4;
	u32 unk5;
} __attribute__ ((packed));

struct ft60x_irqresp {
	int idx;
	short ep;
	short len;
	int val2;
} __attribute__ ((packed));

struct ft60x_device {
	struct usb_device *udev;
	struct ft60x_config ft60x_cfg;
	struct usb_ft60x *ft60x_interface[FT60X_MAX_IF];
	struct ft60x_ctrlreq ctrlreq;
	int respidx;
	int n_interface;
	struct list_head list;
	wait_queue_head_t int_in_wait;	/* to wait for an ongoing read */
};

static LIST_HEAD(ft60x_list);

#define to_ft60x_dev(d) container_of(d, struct usb_ft60x, kref)

static struct usb_driver ft60x_driver;
static void ft60x_draw_down(struct usb_ft60x *dev);
static int ft60x_set_config(struct ft60x_device *ft,
			    struct ft60x_config *ft60x_cfg);

static void ft60x_delete(struct kref *kref)
{
	struct usb_ft60x *dev = to_ft60x_dev(kref);

	if (dev->bulk_in_urb) {
		usb_free_urb(dev->bulk_in_urb);
		dev->bulk_in_urb = NULL;
	}
	if (dev->int_in_urb) {
		usb_free_urb(dev->int_in_urb);
		dev->int_in_urb = NULL;
	}
	usb_put_dev(dev->udev);
	if (dev->int_in_buffer) {
		usb_free_coherent(dev->udev,
				  dev->int_in_size,
				  dev->int_in_buffer, dev->int_in_data_dma);
		dev->int_in_buffer = NULL;
	}
	if (dev->bulk_in_buffer) {
		kfree(dev->bulk_in_buffer);
		dev->bulk_in_buffer = NULL;
	}
	dev->ft->n_interface--;
	if (dev->ft->n_interface == 0) {
		/* last interface, so removing device */
		list_del(&dev->ft->list);
		kfree(dev->ft);
	}
	kfree(dev);

}

static int ft60x_ctrl_req(struct ft60x_device *ft)
{
	int actual_len = 0;
	u8 *b = NULL;
	int retval = 0;

	//printk(KERN_INFO "IN_FUNCTION %s\n", __func__);

	if (!ft) {
		retval = -EINVAL;
		goto exit;
	}

	b = kmalloc(sizeof(struct ft60x_ctrlreq), GFP_KERNEL);
	if (!b) {
		retval = -ENOMEM;
		printk(KERN_ERR "NOT ENOUGH MEM\n");
		goto exit;
	}

	memcpy(b, &ft->ctrlreq, sizeof(struct ft60x_ctrlreq));

	retval = usb_bulk_msg(ft->udev,
			      usb_sndbulkpipe(ft->udev, 1), b,
			      sizeof(struct ft60x_ctrlreq), &actual_len, 1000);
	if (retval) {
		printk("%s: command bulk message failed: error %d\n",
		       __func__, retval);
		goto exit;
	}

exit:
	if (b) {
		kfree(b);
	}

	return retval;
}

static int ft60x_open(struct inode *inode, struct file *file)
{
	struct usb_ft60x *dev;
	struct usb_interface *interface;
	int subminor;
	int retval = 0;

	subminor = iminor(inode);

	interface = usb_find_interface(&ft60x_driver, subminor);
	if (!interface) {
		pr_err("%s - error, can't find device for minor %d\n",
		       __func__, subminor);
		retval = -ENODEV;
		goto exit;
	}

	dev = usb_get_intfdata(interface);
	if (!dev) {
		retval = -ENODEV;
		goto exit;
	}

	retval = usb_autopm_get_interface(interface);
	if (retval)
		goto exit;

	/* increment our usage count for the device */
	kref_get(&dev->kref);

	/* save our object in the file's private structure */
	file->private_data = dev;

exit:
	return retval;
}

static int ft60x_release(struct inode *inode, struct file *file)
{
	struct usb_ft60x *dev;
	struct ft60x_device *ft;

	dev = file->private_data;
	if (dev == NULL)
		return -ENODEV;

	ft = dev->ft;
	ft->ctrlreq.idx++;
	ft->ctrlreq.pipe = dev->bulk_in_endpointAddr;
	ft->ctrlreq.cmd = 0;
	ft->ctrlreq.unk1 = 0;
	ft->ctrlreq.unk2 = 0;
	ft->ctrlreq.len = 0;
	ft->ctrlreq.unk4 = 0;
	ft->ctrlreq.unk5 = 0;
	ft60x_ctrl_req(ft);

	ft->ctrlreq.idx++;
	ft->ctrlreq.pipe = dev->bulk_in_endpointAddr;
	ft->ctrlreq.cmd = 3;
	ft->ctrlreq.unk1 = 0;
	ft->ctrlreq.unk2 = 0;
	ft->ctrlreq.len = 0;
	ft->ctrlreq.unk4 = 0;
	ft->ctrlreq.unk5 = 0;
	ft60x_ctrl_req(ft);

	/* allow the device to be autosuspended */
	mutex_lock(&dev->io_mutex);
	if (dev->interface)
		usb_autopm_put_interface(dev->interface);
	mutex_unlock(&dev->io_mutex);

	/* decrement the count on our device */
	kref_put(&dev->kref, ft60x_delete);
	return 0;
}

static unsigned int ft60x_poll(struct file *file, poll_table * wait)
{
	struct usb_ft60x *dev;
	unsigned int mask = 0;
	char notif;

	//printk(KERN_INFO "IN_FUNCTION %s\n", __func__);

	dev = file->private_data;
	if (!dev->interface)
		return POLLERR | POLLHUP;

	notif = (dev->ft->ft60x_cfg.OptionalFeatureSupport >> dev->bulk_out_endpointAddr) & 1;

	poll_wait(file, &dev->bulk_in_wait, wait);
	//poll_wait(file, &dev->bulk_out_wait, wait);
	poll_wait(file, &dev->int_in_wait, wait);

	/* if no notif, we always can read/write, better not use poll */
	if (!notif) {
		mask |= POLLIN | POLLRDNORM | POLLOUT | POLLWRNORM;
		return mask;
	}

	if (((dev->bulk_in_filled - dev->bulk_in_copied) && dev->done_reading)
	    || dev->got_int) {
		dev->got_int = 0;
		mask |= POLLIN | POLLRDNORM;
	}
	if (!dev->busy_write) {
		mask |= POLLOUT | POLLWRNORM;
	}
	return mask;
}

static int ft60x_flush(struct file *file, fl_owner_t id)
{
	struct usb_ft60x *dev;
	int res;

	dev = file->private_data;
	if (dev == NULL)
		return -ENODEV;

	/* wait for io to stop */
	mutex_lock(&dev->io_mutex);
	ft60x_draw_down(dev);

	/* read out errors, leave subsequent opens a clean slate */
	spin_lock_irq(&dev->err_lock);
	res = dev->errors ? (dev->errors == -EPIPE ? -EPIPE : -EIO) : 0;
	dev->errors = 0;
	spin_unlock_irq(&dev->err_lock);

	mutex_unlock(&dev->io_mutex);

	return res;
}

static void ft60x_int_callback(struct urb *urb)
{
	int retval;
	struct usb_ft60x *dev = urb->context;
	struct usb_ft60x *tmpdev;
	struct ft60x_device *ft = dev->ft;
	struct ft60x_irqresp *irqresp;
	int i;
	int ongoing_read;

	switch (urb->status) {
	case 0:		/* success */
		irqresp = (struct ft60x_irqresp *)dev->int_in_buffer;
		for (i = 0; i < FT60X_MAX_IF; i++) {
			tmpdev = ft->ft60x_interface[i];
			if (tmpdev) {
				if (tmpdev->bulk_in_endpointAddr == irqresp->ep) {
					tmpdev->got_int = 1;
					tmpdev->len_to_read = irqresp->len;
					tmpdev->sent_cmd_read = 0;

					spin_lock(&tmpdev->err_lock);
					ongoing_read = tmpdev->ongoing_read;
					spin_unlock(&tmpdev->err_lock);

					/* we had a read URB going on, have to kill it */
					if (ongoing_read && !tmpdev->waiting_int) {
						usb_unlink_urb(tmpdev->bulk_in_urb);
					}
					if (tmpdev->waiting_int) {
						spin_lock(&tmpdev->err_lock);
						tmpdev->ongoing_read = 0;
						spin_unlock(&tmpdev->err_lock);
						tmpdev->waiting_int = 0;
					}
					wake_up_interruptible(&tmpdev->int_in_wait);
					wake_up_interruptible(&tmpdev->bulk_in_wait);
				}
			}
		}
		break;
	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
		/* this urb is terminated, clean up */
		printk(KERN_INFO "%s - urb shutting down with status: %d\n",
		       __func__, urb->status);
		return;
	default:
		printk(KERN_INFO "%s - nonzero urb status received: %d\n",
		       __func__, urb->status);
		goto exit;
	}

exit:
	retval = usb_submit_urb(urb, GFP_ATOMIC);
	if (retval) {
		printk(KERN_ERR "%s - usb_submit_urb failed with result: %d\n",
		       __func__, retval);
	}
}

static void ft60x_read_bulk_callback(struct urb *urb)
{
	struct usb_ft60x *dev;

	//printk(KERN_INFO "IN_FUNCTION %s\n", __func__);

	dev = urb->context;
	spin_lock(&dev->err_lock);
	/* sync/async unlink faults aren't errors */
	if (urb->status) {
		if (!(urb->status == -ENOENT ||
		      urb->status == -ECONNRESET || urb->status == -ESHUTDOWN))
			dev_err(&dev->interface->dev,
				"%s - nonzero write bulk status received: %d\n",
				__func__, urb->status);

		dev->errors = urb->status;
	}
	dev->bulk_in_filled = urb->actual_length;
	dev->len_to_read -= urb->actual_length;
	dev->ongoing_read = 0;
	spin_unlock(&dev->err_lock);

	wake_up_interruptible(&dev->bulk_in_wait);
}

static int ft60x_send_cmd_read(struct usb_ft60x *dev, size_t count)
{
	int retval = 0;
	char notif;

	if (!dev) {
		retval = -ENODEV;
		goto exit;
	}
	notif = (dev->ft->ft60x_cfg.OptionalFeatureSupport >> dev->bulk_out_endpointAddr) & 1;
	dev->ft->ctrlreq.idx++;
	dev->ft->ctrlreq.pipe = dev->bulk_in_endpointAddr;
	dev->ft->ctrlreq.cmd = 1;
	if (notif) {
		dev->ft->ctrlreq.len = min(dev->bulk_in_size * 128, dev->len_to_read);
	} else {
		dev->ft->ctrlreq.len = dev->bulk_in_size * 128;
	}
	retval = ft60x_ctrl_req(dev->ft);
	dev->sent_cmd_read = 1;
exit:
	return retval;
}

static int ft60x_do_read_io(struct usb_ft60x *dev, size_t count)
{
	int rv = 0;
	char notif;

	//printk(KERN_INFO "IN_FUNCTION %s\n", __func__);

	/* If we are in notification mode */
	notif = (dev->ft->ft60x_cfg.OptionalFeatureSupport >> dev->bulk_out_endpointAddr) & 1;
	if (notif) {
		if ((dev->len_to_read > 0) && !dev->sent_cmd_read) {
			ft60x_send_cmd_read(dev, count);
		}
	} else {
		ft60x_send_cmd_read(dev, count);
	}

	/* Can't read now ! */
	if (notif && (dev->len_to_read <= 0)) {

		spin_lock_irq(&dev->err_lock);
		dev->ongoing_read = 1;
		dev->waiting_int = 1;
		spin_unlock_irq(&dev->err_lock);
		return rv;
	}

	if (notif) {
		usb_fill_bulk_urb(dev->bulk_in_urb,
				  dev->udev,
				  usb_rcvbulkpipe(dev->udev,
						  dev->bulk_in_endpointAddr),
				  dev->bulk_in_buffer,
				  min(dev->bulk_in_size * 128,
				      dev->len_to_read),
				  ft60x_read_bulk_callback, dev);
	} else {
		usb_fill_bulk_urb(dev->bulk_in_urb,
				  dev->udev,
				  usb_rcvbulkpipe(dev->udev,
						  dev->bulk_in_endpointAddr),
				  dev->bulk_in_buffer,
				  dev->bulk_in_size * 128,
				  ft60x_read_bulk_callback, dev);
	}

	/* tell everybody to leave the URB alone */
	spin_lock_irq(&dev->err_lock);
	dev->ongoing_read = 1;
	spin_unlock_irq(&dev->err_lock);

	/* submit bulk in urb, which means no data to deliver */
	dev->bulk_in_filled = 0;
	dev->bulk_in_copied = 0;

	/* do it */
	rv = usb_submit_urb(dev->bulk_in_urb, GFP_KERNEL);

	if (rv < 0) {
		dev_err(&dev->interface->dev,
			"%s - failed submitting read urb, error %d\n",
			__func__, rv);
		rv = (rv == -ENOMEM) ? rv : -EIO;
		spin_lock_irq(&dev->err_lock);
		dev->ongoing_read = 0;
		spin_unlock_irq(&dev->err_lock);
	}
	return rv;
}

static ssize_t ft60x_read(struct file *file, char *buffer, size_t count,
			  loff_t * ppos)
{
	struct usb_ft60x *dev;
	int rv;
	bool ongoing_io;

	//printk(KERN_INFO "IN_FUNCTION %s\n", __func__);

	dev = file->private_data;

	/* if we cannot read at all, return EOF */
	if (!dev->bulk_in_urb || !count)
		return 0;

	/* no concurrent readers */
	rv = mutex_lock_interruptible(&dev->io_mutex);
	if (rv < 0)
		return rv;

	if (!dev->interface) {	/* disconnect() was called */
		rv = -ENODEV;
		goto exit;
	}

	dev->done_reading = 0;
	/* if IO is under way, we must not touch things */
retry:
	spin_lock_irq(&dev->err_lock);
	ongoing_io = dev->ongoing_read;
	spin_unlock_irq(&dev->err_lock);

	if (ongoing_io) {
		/* nonblocking IO shall not wait */
		if (file->f_flags & O_NONBLOCK) {
			rv = -EAGAIN;
			goto exit;
		}
		/*
		 * IO may take forever
		 * hence wait in an interruptible state
		 */
		rv = wait_event_interruptible(dev->bulk_in_wait,
					      (!dev->ongoing_read));
		if (rv < 0)
			goto exit;
	}

	/* errors must be reported */
	rv = dev->errors;
	if (rv == -ECONNRESET) {
		dev->errors = 0;
		if (!dev->bulk_in_filled) {
			rv = ft60x_send_cmd_read(dev, count);
			if (rv < 0)
				goto exit;
			printk(KERN_INFO "FROM here 1 %d \n",
			       dev->bulk_in_urb->actual_length);
			rv = ft60x_do_read_io(dev, count);
			if (rv < 0) {
				goto exit;
			} else
				goto retry;
		}
	}

	if ((rv < 0) && (rv != -ECONNRESET)) {
		/* any error is reported once */
		dev->errors = 0;
		/* to preserve notifications about reset */
		rv = (rv == -EPIPE) ? rv : -EIO;
		/* report it */
		goto exit;
	}

	/*
	 * if the buffer is filled we may satisfy the read
	 * else we need to start IO
	 */
	if (dev->bulk_in_filled) {
		/* we had read data */
		size_t available = dev->bulk_in_filled - dev->bulk_in_copied;
		size_t chunk = min(available, count);

		if (!available) {
			/*
			 * all data has been used
			 * actual IO needs to be done
			 */
			rv = ft60x_do_read_io(dev, count);
			if (rv < 0)
				goto exit;
			else
				goto retry;
		}

		/*
		 * data is available
		 * chunk tells us how much shall be copied
		 */
		if (copy_to_user(buffer,
				 dev->bulk_in_buffer + dev->bulk_in_copied,
				 chunk))
			rv = -EFAULT;
		else {
			rv = chunk;
		}
		dev->bulk_in_copied += chunk;

		/*
		 * if we are asked for more than we have,
		 * we start IO but don't wait
		 */
		if (available < count) {
			ft60x_do_read_io(dev, count - chunk);
		}
	} else {
		/* no data in the buffer */
		rv = ft60x_do_read_io(dev, count);

		if (rv < 0)
			goto exit;
		else
			goto retry;
	}
exit:
	dev->done_reading = 1;
	mutex_unlock(&dev->io_mutex);
	return rv;
}

static void ft60x_write_bulk_callback(struct urb *urb)
{
	struct usb_ft60x *dev;

	dev = urb->context;

	/* sync/async unlink faults aren't errors */
	if (urb->status) {
		if (!(urb->status == -ENOENT ||
		      urb->status == -ECONNRESET || urb->status == -ESHUTDOWN))
			dev_err(&dev->interface->dev,
				"%s - nonzero write bulk status received: %d\n",
				__func__, urb->status);

		spin_lock(&dev->err_lock);
		dev->busy_write = 0;
		dev->errors = urb->status;
		spin_unlock(&dev->err_lock);
		wake_up_interruptible(&dev->bulk_out_wait);
	}

	/* free up our allocated buffer */
	usb_free_coherent(urb->dev, urb->transfer_buffer_length,
			  urb->transfer_buffer, urb->transfer_dma);
	up(&dev->limit_sem);
}

static ssize_t ft60x_write(struct file *file, const char *user_buffer,
			   size_t count, loff_t * ppos)
{
	struct usb_ft60x *dev;
	int retval = 0;
	struct urb *urb = NULL;
	char *buf = NULL;
	size_t writesize = min(count, (size_t) MAX_TRANSFER);

	dev = file->private_data;

	/* verify that we actually have some data to write */
	if (count == 0)
		goto exit;

	/*
	 * limit the number of URBs in flight to stop a user from using up all
	 * RAM
	 */
	if (!(file->f_flags & O_NONBLOCK)) {
		if (down_interruptible(&dev->limit_sem)) {
			retval = -ERESTARTSYS;
			goto exit;
		}
	} else {
		if (down_trylock(&dev->limit_sem)) {
			retval = -EAGAIN;
			goto exit;
		}
	}

	spin_lock_irq(&dev->err_lock);
	retval = dev->errors;
	if (retval < 0) {
		/* any error is reported once */
		dev->errors = 0;
		/* to preserve notifications about reset */
		retval = (retval == -EPIPE) ? retval : -EIO;
	}
	spin_unlock_irq(&dev->err_lock);
	if (retval < 0)
		goto error;

	/* create a urb, and a buffer for it, and copy the data to the urb */
	urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!urb) {
		retval = -ENOMEM;
		goto error;
	}

	buf = usb_alloc_coherent(dev->udev, writesize, GFP_KERNEL,
				 &urb->transfer_dma);
	if (!buf) {
		retval = -ENOMEM;
		goto error;
	}

	if (copy_from_user(buf, user_buffer, writesize)) {
		retval = -EFAULT;
		goto error;
	}

	/* this lock makes sure we don't submit URBs to gone devices */
	mutex_lock(&dev->io_mutex);
	if (!dev->interface) {	/* disconnect() was called */
		mutex_unlock(&dev->io_mutex);
		retval = -ENODEV;
		goto error;
	}

	/* initialize the urb properly */
	usb_fill_bulk_urb(urb, dev->udev,
			  usb_sndbulkpipe(dev->udev,
					  dev->bulk_out_endpointAddr), buf,
			  writesize, ft60x_write_bulk_callback, dev);
	urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
	usb_anchor_urb(urb, &dev->submitted);

	/* send the data out the bulk port */
	retval = usb_submit_urb(urb, GFP_KERNEL);
	mutex_unlock(&dev->io_mutex);
	if (retval) {
		dev_err(&dev->interface->dev,
			"%s - failed submitting write urb, error %d\n",
			__func__, retval);
		goto error_unanchor;
	}
	spin_lock_irq(&dev->err_lock);
	dev->busy_write = 1;
	spin_unlock_irq(&dev->err_lock);

	/*
	 * release our reference to this urb, the USB core will eventually free
	 * it entirely
	 */
	usb_free_urb(urb);

	return writesize;

error_unanchor:
	usb_unanchor_urb(urb);
error:
	if (urb) {
		usb_free_coherent(dev->udev, writesize, buf, urb->transfer_dma);
		usb_free_urb(urb);
	}
	up(&dev->limit_sem);

exit:
	return retval;
}

static long ft60x_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	long retval = 0;
	struct usb_ft60x *dev;
	struct ft60x_config cfg;

	dev = file->private_data;

	if (!dev) {
		retval = -ENODEV;
		goto exit;
	}
	switch (cmd) {
	case 0:
		if (copy_to_user((unsigned char *)arg,
				 (unsigned char *)&dev->ft->ft60x_cfg,
				 sizeof(struct ft60x_config)))
			retval = -EFAULT;
		break;
	case 1:
		if (copy_from_user
		    (&cfg, (unsigned char *)arg, sizeof(struct ft60x_config))) {
			retval = -EFAULT;
			goto exit;
		}
		ft60x_set_config(dev->ft, &cfg);
		break;
	default:
		printk(KERN_ERR "Unknown IOCTL %d\n", cmd);
	}

exit:
	return retval;
}

static const struct file_operations ft60x_fops = {
	.owner = 	THIS_MODULE,
	.read = 	ft60x_read,
	.write = 	ft60x_write,
	.open = 	ft60x_open,
	.release = 	ft60x_release,
	.flush = 	ft60x_flush,
	.poll = 	ft60x_poll,
	.unlocked_ioctl = 	ft60x_ioctl,
	.llseek = 	noop_llseek,
};

/*
 * usb class driver info in order to get a minor number from the usb core,
 * and to have the device registered with the driver core
 */
static struct usb_class_driver ft60x_class = {
	.name = "ft60x%d",
	.fops = &ft60x_fops,
	.minor_base = USB_FT60X_MINOR_BASE,
};

static int ft60x_set_config(struct ft60x_device *ft,
			    struct ft60x_config *ft60x_cfg)
{
	struct ft60x_config *cfg = NULL;
	int ret;

	if (!ft || !ft60x_cfg) {
		ret = -EINVAL;
		goto exit;
	}

	/* Make sure we are not sending the same thing because it'd disconnect us */
	if (!memcmp(ft60x_cfg, &ft->ft60x_cfg, sizeof(struct ft60x_config))) {
		return 0;
	}

	/* Changing configuration */
	cfg = kmalloc(sizeof(struct ft60x_config), GFP_NOIO);
	if (!cfg) {
		ret = -ENOMEM;
		goto exit;
	}

	memcpy(cfg, ft60x_cfg, sizeof(struct ft60x_config));

	ret = usb_control_msg(ft->udev, usb_sndctrlpipe(ft->udev, 0),
			      0xcf, USB_TYPE_VENDOR | USB_DIR_OUT, 0, 0,
			      cfg, sizeof(struct ft60x_config),
			      USB_CTRL_SET_TIMEOUT);

	if (ret < 0) {
		ret = -EIO;
		goto exit;
	}
exit:
	if (cfg)
		kfree(cfg);

	return ret;
}

static int ft60x_get_config(struct ft60x_device *ft)
{
	struct ft60x_config *cfg = NULL;
	int retval;
	int ret;

	if (!ft) {
		retval = -EINVAL;
		goto exit;
	}

	cfg = kmalloc(sizeof(struct ft60x_config), GFP_NOIO);
	if (!cfg) {
		retval = -ENOMEM;
		goto exit;
	}

	ret = usb_control_msg(ft->udev, usb_rcvctrlpipe(ft->udev, 0),
			      0xcf, USB_TYPE_VENDOR | USB_DIR_IN, 1, 0,
			      cfg, sizeof(struct ft60x_config),
			      USB_CTRL_SET_TIMEOUT);
	if (ret < 0) {
		retval = -EIO;
		goto exit;
	}

	if (ret <= sizeof(struct ft60x_config)) {
		memcpy(&ft->ft60x_cfg, cfg, ret);
	}

exit:
	if (cfg) {
		kfree(cfg);
	}
	return retval;
}

static int ft60x_get_unknown(struct ft60x_device *ft)
{
	struct ft60x_config *cfg = NULL;
	int retval;
	int ret;
	unsigned int *val = NULL;
	if (!ft) {
		retval = -EINVAL;
		goto exit;
	}

	val = kmalloc(sizeof(unsigned int), GFP_NOIO);
	if (!val) {
		retval = -ENOMEM;
		goto exit;
	}

	ret = usb_control_msg(ft->udev, usb_rcvctrlpipe(ft->udev, 0),
			      0xf1, USB_TYPE_VENDOR | USB_DIR_IN, 0, 0,
			      val, 4, USB_CTRL_SET_TIMEOUT);
	if (ret < 0) {
		retval = -EIO;
		printk(KERN_ERR "GOT ERROR1: %d\n", retval);
		goto exit;
	}

exit:
	if (val) {
		kfree(cfg);
	}
	return retval;
}

static int ft60x_alloc_data_interface(struct usb_ft60x *dev,
				      struct usb_host_interface *iface_desc)
{
	int i;
	size_t buffer_size;
	int retval = 0;
	struct usb_endpoint_descriptor *endpoint;

	for (i = 0; i < iface_desc->desc.bNumEndpoints; ++i) {
		endpoint = &iface_desc->endpoint[i].desc;

		if (!dev->bulk_in_endpointAddr &&
		    usb_endpoint_is_bulk_in(endpoint)) {
			/* we found a bulk in endpoint */
			buffer_size = usb_endpoint_maxp(endpoint);
			dev->bulk_in_size = buffer_size;
			dev->bulk_in_endpointAddr = endpoint->bEndpointAddress;
			dev->bulk_in_buffer =
			    kmalloc(buffer_size * 128, GFP_KERNEL);
			if (!dev->bulk_in_buffer) {
				retval = -ENOMEM;
				goto error;
			}

			dev->bulk_in_urb = usb_alloc_urb(0, GFP_KERNEL);
			if (!dev->bulk_in_urb) {
				retval = -ENOMEM;
				goto error;
			}
		}
		if (!dev->bulk_out_endpointAddr &&
		    usb_endpoint_is_bulk_out(endpoint)) {
			/* we found a bulk out endpoint */
			dev->bulk_out_endpointAddr = endpoint->bEndpointAddress;
		}
	}

error:
	return retval;
}

static int ft60x_alloc_ctrl_interface(struct usb_ft60x *dev,
				      struct usb_host_interface *iface_desc)
{
	int i;
	size_t buffer_size;
	int retval = 0;
	int maxp, pipe;
	struct usb_endpoint_descriptor *endpoint;

	for (i = 0; i < iface_desc->desc.bNumEndpoints; ++i) {
		endpoint = &iface_desc->endpoint[i].desc;

		if (!dev->int_in_endpointAddr &&
		    usb_endpoint_is_int_in(endpoint)) {
			/* we found a int in endpoint */
			buffer_size = usb_endpoint_maxp(endpoint);
			dev->int_in_size = buffer_size;
			dev->int_in_endpointAddr = endpoint->bEndpointAddress;
			dev->int_in_buffer = usb_alloc_coherent(dev->udev,
								dev->
								int_in_size,
								GFP_ATOMIC,
								&dev->
								int_in_data_dma);
			if (!dev->int_in_buffer) {
				retval = -ENOMEM;
				goto error;
			}

			dev->int_in_urb = usb_alloc_urb(0, GFP_KERNEL);
			if (!dev->int_in_urb) {
				retval = -ENOMEM;
				goto error;
			}

			/* get a handle to the interrupt data pipe */
			pipe = usb_rcvintpipe(dev->udev,
					      endpoint->bEndpointAddress);
			maxp = usb_maxpacket(dev->udev, pipe, usb_pipeout(pipe));
			usb_fill_int_urb(dev->int_in_urb, dev->udev, pipe,
					 dev->int_in_buffer, maxp,
					 ft60x_int_callback, dev,
					 endpoint->bInterval);
			dev->int_in_urb->transfer_dma = dev->int_in_data_dma;
			dev->int_in_urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
			/* register our interrupt URB with the USB system */

			if (usb_submit_urb(dev->int_in_urb, GFP_KERNEL)) {
				retval = -EIO;
				goto error;
			}
		}

		if (!dev->bulk_out_endpointAddr &&
		    usb_endpoint_is_bulk_out(endpoint)) {
			/* we found a bulk out endpoint */
			dev->bulk_out_endpointAddr = endpoint->bEndpointAddress;
		}
	}

error:
	return retval;
}

static void ft60x_print_usb_log(struct usb_interface *intf,
				const struct usb_device_id *id)
{
	int i;
	struct usb_host_interface *interface;
	struct usb_endpoint_descriptor *endpoint;

	if (!intf || !id) {
		printk(KERN_ERR "Invalid arg\n");
		return;
	}
	interface = intf->cur_altsetting;
	printk(KERN_INFO "FT60x i/f %d now probed: (%04X:%04X)\n",
	       interface->desc.bInterfaceNumber, id->idVendor, id->idProduct);
	printk(KERN_INFO "ID->bNumEndpoints: %02X\n",
	       interface->desc.bNumEndpoints);
	printk(KERN_INFO "ID->bInterfaceClass: %02X\n",
	       interface->desc.bInterfaceClass);

	for (i = 0; i < interface->desc.bNumEndpoints; i++) {
		endpoint = &interface->endpoint[i].desc;
		printk(KERN_INFO "ED[%d]->bEndpointAddress: 0x%02X\n",
		       i, endpoint->bEndpointAddress);
		printk(KERN_INFO "ED[%d]->bmAttributes: 0x%02X\n",
		       i, endpoint->bmAttributes);
		printk(KERN_INFO "ED[%d]->wMaxPacketSize: 0x%04X (%d)\n",
		       i, endpoint->wMaxPacketSize, endpoint->wMaxPacketSize);
	}
}

static int ft60x_alloc_device(struct usb_device *device,
			      struct ft60x_device **ftout)
{
	struct ft60x_device *ft = NULL;

	int retval = 0;

	if (!ftout || !device) {
		retval = -EINVAL;
		goto exit;
	}

	ft = kzalloc(sizeof(struct ft60x_device), GFP_KERNEL);
	if (!ft) {
		retval = -ENOMEM;
		goto exit;
	}
	ft->udev = device;
	init_waitqueue_head(&ft->int_in_wait);
	list_add_tail(&(ft->list), &(ft60x_list));
exit:
	*ftout = ft;
	return retval;
}

static int ft60x_find_device(struct usb_device *dev,
			     struct ft60x_device **ftout)
{
	struct ft60x_device *ft = NULL;
	int retval = 0;

	if (!dev || !ftout) {
		retval = -EINVAL;
		goto exit;
	}

	list_for_each_entry(ft, &ft60x_list, list) {
		if (ft->udev == dev)
			goto exit;
	}
	ft = NULL;
exit:
	*ftout = ft;
	return retval;
}

static int ft60x_probe(struct usb_interface *interface,
		       const struct usb_device_id *id)
{
	struct usb_ft60x *dev = NULL;
	struct usb_host_interface *iface_desc;
	struct usb_device *device;
	struct ft60x_device *ft = NULL;
	int retval = -ENOMEM;
	int ifn;

	device = interface_to_usbdev(interface);
	ft60x_print_usb_log(interface, id);

	ft60x_find_device(device, &ft);
	if (!ft) {
		retval = ft60x_alloc_device(device, &ft);
		if (retval) {
			goto error;
		}
	}

	/* allocate memory for our device state and initialize it */
	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		goto error;
	dev->ft = ft;
	ft->n_interface++;
	kref_init(&dev->kref);
	sema_init(&dev->limit_sem, WRITES_IN_FLIGHT);
	mutex_init(&dev->io_mutex);

	spin_lock_init(&dev->err_lock);
	init_usb_anchor(&dev->submitted);
	init_waitqueue_head(&dev->bulk_in_wait);
	init_waitqueue_head(&dev->bulk_out_wait);
	init_waitqueue_head(&dev->int_in_wait);

	dev->udev = usb_get_dev(device);
	dev->interface = interface;
	dev->done_reading = 1;
	/* set up the endpoint information */
	/* use only the first bulk-in and bulk-out endpoints */
	iface_desc = interface->cur_altsetting;

	ifn = iface_desc->desc.bInterfaceNumber;
	ft->ft60x_interface[ifn] = dev;

	if (ifn == 0) {
		if ((retval = ft60x_alloc_ctrl_interface(dev, iface_desc)) < 0)
			goto error;
		if (!(dev->int_in_endpointAddr && dev->bulk_out_endpointAddr)) {
			dev_err(&interface->dev,
				"Could not find both int-in \n");
			goto error;
		}
		ft60x_get_config(ft);
		/* 4 bytes the FTDI gets we don't know what it is (0x00000109) */
		ft60x_get_unknown(ft);
	} else {
		if ((retval = ft60x_alloc_data_interface(dev, iface_desc)) < 0)
			goto error;

		if (!(dev->bulk_in_endpointAddr && dev->bulk_out_endpointAddr)) {
			dev_err(&interface->dev,
				"Could not find both bulk-in and bulk-out endpoints\n");
			goto error;
		}
		/* we can register the device now, as it is ready */
		retval = usb_register_dev(interface, &ft60x_class);
		if (retval) {
			/* something prevented us from registering this driver */
			dev_err(&interface->dev,
				"Not able to get a minor for this device.\n");
			usb_set_intfdata(interface, NULL);
			goto error;
		}
		/* let the user know what node this device is now attached to */
		dev_info(&interface->dev,
			 "USB FT60x device now attached to ft60x-%d",
			 interface->minor);
	}

	/* save our data pointer in this interface device */
	usb_set_intfdata(interface, dev);

	return 0;

error:
	if (dev)
		/* this frees allocated memory */
		kref_put(&dev->kref, ft60x_delete);
	return retval;
}

static void ft60x_disconnect(struct usb_interface *interface)
{
	struct usb_ft60x *dev;
	int minor = interface->minor;

	dev = usb_get_intfdata(interface);
	usb_set_intfdata(interface, NULL);

	/* give back our minor */
	usb_deregister_dev(interface, &ft60x_class);

	/* prevent more I/O from starting */
	mutex_lock(&dev->io_mutex);
	dev->interface = NULL;
	mutex_unlock(&dev->io_mutex);

	usb_kill_anchored_urbs(&dev->submitted);

	/* decrement our usage count */
	kref_put(&dev->kref, ft60x_delete);

	dev_info(&interface->dev, "USB FT60x #%d now disconnected", minor);
}

static void ft60x_draw_down(struct usb_ft60x *dev)
{
	int time;

	time = usb_wait_anchor_empty_timeout(&dev->submitted, 1000);
	if (!time)
		usb_kill_anchored_urbs(&dev->submitted);
	usb_kill_urb(dev->bulk_in_urb);
}

static int ft60x_suspend(struct usb_interface *intf, pm_message_t message)
{
	struct usb_ft60x *dev = usb_get_intfdata(intf);

	if (!dev)
		return 0;
	ft60x_draw_down(dev);
	return 0;
}

static int ft60x_resume(struct usb_interface *intf)
{
	return 0;
}

static int ft60x_pre_reset(struct usb_interface *intf)
{
	struct usb_ft60x *dev = usb_get_intfdata(intf);

	mutex_lock(&dev->io_mutex);
	ft60x_draw_down(dev);

	return 0;
}

static int ft60x_post_reset(struct usb_interface *intf)
{
	struct usb_ft60x *dev = usb_get_intfdata(intf);

	/* we are sure no URBs are active - no locking needed */
	dev->errors = -EPIPE;
	mutex_unlock(&dev->io_mutex);

	return 0;
}

static struct usb_driver ft60x_driver = {
	.name = 	"ft60x",
	.probe = 	ft60x_probe,
	.disconnect = 	ft60x_disconnect,
	.suspend = 	ft60x_suspend,
	.resume = 	ft60x_resume,
	.pre_reset = 	ft60x_pre_reset,
	.post_reset = 	ft60x_post_reset,
	.id_table = 	ft60x_table,
	.supports_autosuspend = 1,
};

module_usb_driver(ft60x_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ramtin Amin <ramtin@lambdaconcept.com>");
MODULE_DESCRIPTION("FT60x Driver");
