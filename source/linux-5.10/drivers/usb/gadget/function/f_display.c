// SPDX-License-Identifier: GPL-2.0+
/*
 * f_display.c -- USB Display (display) function driver
 *
 * Copyright (C) 2020-2026 ArtInChip Technology Co., Ltd.
 * Authors:  che.jiang <che.jiang@artinchip.com>
 */

/* #define VERBOSE_DEBUG */

#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/miscdevice.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/poll.h>
#include <linux/uaccess.h>
#include <linux/dma-mapping.h>

#include "u_display.h"

#define FRAME_CMD_FLAG        0xA1C62B00
#define FRAME_CMD_MSK         0xFFFFFF00
#define FRAME_USB_FRAG_HEAD   0x01
#define FRAME_USB_FRAG_MID    0x0
#define FRAME_USB_FRAG_END    0x04
#define FRAME_START_MAGIC     (FRAME_CMD_FLAG | FRAME_USB_FRAG_HEAD)
#define FRAME_END_MAGIC       (FRAME_CMD_FLAG | FRAME_USB_FRAG_END)

#define FRAME_CMD_SIZE        20
#define FRAME_BUFFER_SIZE     (1024 * 1024 * 2)
#define MAX_FRAMES_NUM        8
#define MAX_BULK_OUT_REQUESTS 16
#define BULK_BUFFER_NUM       (128)
#define BULK_BUFFER_SIZE      (512)

#define DISP_IOC_MAGIC        'd'
#define DISP_DQ_FRAME         _IOR(DISP_IOC_MAGIC, 1, struct frame_buffer_info)
#define DISP_Q_FRAME          _IOW(DISP_IOC_MAGIC, 2, struct frame_buffer_info)
#define DISP_BUF_CNT          _IOR(DISP_IOC_MAGIC, 3, int)
#define DISP_IOC_MAXNR        3

struct dma_buffer {
	int              size;
	void            *start;
	dma_addr_t       start_dma;
};

struct frame_buffer_info {
	void __user     *data;
	unsigned int     phy_addr;
	unsigned int     id;
	unsigned int     offset;
	size_t           size;
};


struct ctrl_response {
	struct usb_ctrlrequest ctrl;
	unsigned short   data_length;
	unsigned char    data[512];
};

struct display_bulk_buffer {
	struct list_head list;
	size_t           len;
	bool             used;
	void             *data;
};

struct frame_cmd {
	unsigned int     sMagic;
	unsigned int     Length;
	unsigned short   Id;
	unsigned short   Rotate;
} __packed;

struct frame_buffer {
	void            *data;
	unsigned int     phy_addr;
	size_t           size;
	unsigned int     offset;
	bool             in_use;
	unsigned int     id;
	struct list_head list;
};

struct f_display {
	struct usb_composite_dev *cdev;
	struct gdisplay         port;
	unsigned char           data_id;

	// Bulk buffer management
	wait_queue_head_t       bulk_waitq;
	struct list_head        bulk_list;
	spinlock_t              bulk_lock;
	bool                    bulk_enabled;
	struct display_bulk_buffer *bulk_buffer_pool;
	int                      next_buffer_idx;

	struct usb_request     *bulk_out_reqs[MAX_BULK_OUT_REQUESTS];
	int                     num_bulk_out_reqs;

	// Frame buffer management
	struct dma_buffer       frame_buffer_dma;
	unsigned int            frame_buffer_offset;
	struct frame_buffer     frames[MAX_FRAMES_NUM];
	struct list_head        frame_list;  // List of frames being collected
	struct list_head        empty_list;  // List of empty frames
	int                     frame_count; // current frame count
	int                     empty_count; // current empty count
	int                     frame_id;    // current frame id
	spinlock_t              frame_lock;  // frame buffer lock
	wait_queue_head_t       frame_waitq; // frame buffer wait queue

	// Current frame collection state
	void                   *cur_frame_data; // Buffer for current frame being collected
	size_t                  cur_frame_size; // Expected size of current frame
	size_t                  cur_frame_collected; // Amount of data collected so far
	bool                    collecting_frame;   // Whether we're currently collecting a frame
};

struct f_display_request_entry {
	struct list_head        list;
	struct usb_ctrlrequest  ctrl;
};

static DEFINE_MUTEX(g_request_lock);
static LIST_HEAD(g_pending_requests);
static wait_queue_head_t g_request_waitq;
static DEFINE_MUTEX(g_response_lock);
static struct completion g_response_completion;
static struct miscdevice g_ctrl_miscdev; // For Control request
static struct miscdevice g_bulk_miscdev;  // For bulk data

static struct f_display *g_display_context;

static inline struct f_display *func_to_display(struct usb_function *f)
{
	return container_of(f, struct f_display, port.func);
}

static struct usb_interface_descriptor display_data_interface_desc = {
	.bLength =		USB_DT_INTERFACE_SIZE,
	.bDescriptorType =	USB_DT_INTERFACE,
	/* .bInterfaceNumber = DYNAMIC */
	.bNumEndpoints =	2,
	.bInterfaceClass =	0xFF,
	.bInterfaceSubClass =	0x00,
	.bInterfaceProtocol =	0x00,
	.iInterface = 0
};

static struct usb_endpoint_descriptor display_fs_in_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,
	.bEndpointAddress =	USB_DIR_IN | 0x1,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize = cpu_to_le16(64),
};

static struct usb_endpoint_descriptor display_fs_out_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,
	.bEndpointAddress =	USB_DIR_OUT | 0x1,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize = cpu_to_le16(64),
};

static struct usb_descriptor_header *display_fs_function[] = {
	(struct usb_descriptor_header *)&display_data_interface_desc,
	(struct usb_descriptor_header *)&display_fs_in_desc,
	(struct usb_descriptor_header *)&display_fs_out_desc,
	NULL,
};

static struct usb_endpoint_descriptor display_hs_in_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,
	.bEndpointAddress =	USB_DIR_IN | 0x1,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize =	cpu_to_le16(512),
};

static struct usb_endpoint_descriptor display_hs_out_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,
	.bEndpointAddress =	USB_DIR_OUT | 0x1,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize =	cpu_to_le16(512),
};

static struct usb_descriptor_header *display_hs_function[] = {
	(struct usb_descriptor_header *)&display_data_interface_desc,
	(struct usb_descriptor_header *)&display_hs_in_desc,
	(struct usb_descriptor_header *)&display_hs_out_desc,
	NULL,
};

/* string descriptors: */
#define DISPLAY_DATA_IDX	0

/* static strings, in UTF-8 */
static struct usb_string display_string_defs[] = {
	[DISPLAY_DATA_IDX].s = "USB Display Device",
	{  } /* end of list */
};

static struct usb_gadget_strings display_string_table = {
	.language =		0x0409,	/* en-us */
	.strings =		display_string_defs,
};

static struct usb_gadget_strings *display_strings[] = {
	&display_string_table,
	NULL,
};

/* Bulk data completion handlers */
static void display_bulk_in_complete(struct usb_ep *ep, struct usb_request *req)
{
	if (req->status != 0)
		pr_debug("Display: bulk in complete with status: %d\n", req->status);

	kfree(req->buf);
	usb_ep_free_request(ep, req);
}

static bool display_bulk_frame(struct f_display *display, struct usb_request *req)
{
	struct frame_cmd *cmd = (struct frame_cmd *)req->buf;
	struct frame_buffer *frame = NULL;
	u32 frame_len = 0;
	unsigned long flags;
	void *base_vir_addr;
	int i = 0;


	/* Start frame command */
	if ((req->actual == FRAME_CMD_SIZE) && (cmd->sMagic == FRAME_START_MAGIC)) {
		frame_len = cmd->Length;
		pr_debug("Rec frame start cmd: len = %d, id = %d\n", frame_len, cmd->Id);

		// If we were collecting a previous frame, discard it
		spin_lock_irqsave(&display->frame_lock, flags);

		if (!list_empty(&display->empty_list)) {
			// Initialize new frame collection
			frame = list_first_entry(&display->empty_list, struct frame_buffer, list);
			list_del(&frame->list);
			display->empty_count--;

			base_vir_addr = display->frame_buffer_dma.start;
			if (display->frame_buffer_offset + frame_len >=
					display->frame_buffer_dma.size) {
				display->frame_buffer_offset = 0;
			}
			frame->data = base_vir_addr + display->frame_buffer_offset;
			frame->phy_addr = display->frame_buffer_dma.start_dma +
				display->frame_buffer_offset;

			display->frame_buffer_offset += PAGE_ALIGN(frame_len);

			frame->size = frame_len;
			frame->in_use = true;
			display->frame_id = frame->id;
			display->collecting_frame = true;
			display->cur_frame_data = frame->data;
			display->cur_frame_collected = 0;
			display->cur_frame_size = frame_len;
		}  else {
			pr_debug("Display: No frame buffer available\n");
			display->collecting_frame = false;
			display->cur_frame_data = NULL;
			display->frame_id = -1;
		}

		spin_unlock_irqrestore(&display->frame_lock, flags);
		return true;
	}
	/* Frame command processing (ignore unknown commands) */
	else if ((req->actual == FRAME_CMD_SIZE) && (cmd->sMagic == FRAME_END_MAGIC)) {

		spin_lock_irqsave(&display->frame_lock, flags);
		display->collecting_frame = false;
		display->cur_frame_size = 0;
		display->cur_frame_collected = 0;
		display->cur_frame_data = NULL;
		spin_unlock_irqrestore(&display->frame_lock, flags);
		pr_debug("Rec frame end cmd: len = %d, id = %d\n", frame_len, cmd->Id);
		return true;
	}
	/* Frame data reception */
	else {
		// Only process if we're currently collecting a frame
		if (display->collecting_frame && display->cur_frame_data) {
			// Check for buffer overflow
			spin_lock_irqsave(&display->frame_lock, flags);
			if (display->cur_frame_collected + req->actual
				<= display->cur_frame_size) {
				// Copy data to frame buffer
				memcpy((u8 *)display->cur_frame_data +
						display->cur_frame_collected,
					   req->buf, req->actual);
				display->cur_frame_collected += req->actual;
				pr_debug("Frame data copied: %zd < %zd, request size: %u\n",
					display->cur_frame_collected,
					display->cur_frame_size, req->actual);
			} else {
				pr_err("Frame data overflow. Expect: %zu, Coll: %zu, New: %d\n",
					display->cur_frame_size, display->cur_frame_collected,
					req->actual);
			}
			spin_unlock_irqrestore(&display->frame_lock, flags);

			/* Complete current frame */
			if (display->cur_frame_collected >= display->cur_frame_size) {
				pr_debug("Frame complete %zd, size = %zd\n",
					display->cur_frame_collected, display->cur_frame_size);

				spin_lock(&display->frame_lock);
				// Get current frame node and add to the list
				for (i = 0; i < MAX_FRAMES_NUM; i++) {
					if (display->frames[i].data == display->cur_frame_data) {
						frame = &display->frames[i];
						break;
					}
				}

				if (frame) {
					list_add_tail(&frame->list, &display->frame_list);
					display->frame_count++;
					wake_up(&display->frame_waitq);
					pr_debug("Frame added to queue, count = %d\n",
						display->frame_count);
				} else {
					// Drop frame if we've reached the limit
					pr_err("Frame buffer full, dropping frame\n");
				}
				spin_unlock(&display->frame_lock);

				// Reset frame collection state
				display->cur_frame_data = NULL;
				display->cur_frame_size = 0;
				display->cur_frame_collected = 0;
				display->collecting_frame = false;
			}
			return true;
		}
	}

	return false;
}
static void display_bulk_out_complete(struct usb_ep *ep, struct usb_request *req)
{
	struct f_display *display = req->context;
	struct display_bulk_buffer *buffer = NULL;
	unsigned long flags;
	int i, start_idx;

	if (req->status != 0 && req->status != -ECONNRESET)
		pr_debug("Display: bulk out complete with status: %d\n", req->status);

	if (req->actual > 0) {
		if (display_bulk_frame(display, req))
			goto requeue;

		spin_lock_irqsave(&display->bulk_lock, flags);
		start_idx = display->next_buffer_idx;
		do {
			i = display->next_buffer_idx;
			if (!display->bulk_buffer_pool[i].used) {
				buffer = &display->bulk_buffer_pool[i];
				buffer->used = true;
				display->next_buffer_idx = (i + 1) % BULK_BUFFER_NUM;
				break;
			}
			display->next_buffer_idx = (i + 1) % BULK_BUFFER_NUM;
		} while (display->next_buffer_idx != start_idx);

		if (buffer) {
			buffer->len = req->actual;
			if (req->actual <= BULK_BUFFER_SIZE) {
				memcpy(buffer->data, req->buf, req->actual);
				list_add_tail(&buffer->list, &display->bulk_list);
				spin_unlock_irqrestore(&display->bulk_lock, flags);
				wake_up(&display->bulk_waitq);
			} else {
				buffer->used = false;
				spin_unlock_irqrestore(&display->bulk_lock, flags);
				pr_err("Display: Bulk data too large: %d > %d\n",
					   req->actual, BULK_BUFFER_SIZE);
			}
		} else {
			spin_unlock_irqrestore(&display->bulk_lock, flags);
			pr_debug("Display: No free bulk buffer available\n");
		}
	}

requeue:
	// Re-submit the request if data is still enabled
	if (display->bulk_enabled) {
		req->length = ep->maxpacket;
		if (usb_ep_queue(ep, req, GFP_ATOMIC) < 0)
			pr_err("Display: Failed to re-submit bulk out request\n");
	}
}

static int f_display_ctrl_open(struct inode *inode, struct file *file)
{
	pr_debug("Display: ctrl device opened\n");
	return 0;
}

static int f_display_ctrl_release(struct inode *inode, struct file *file)
{
	pr_debug("Display: ctrl device closed\n");
	return 0;
}

static ssize_t f_display_ctrl_read(struct file *file, char __user *buf,
			size_t count, loff_t *ppos)
{
	struct f_display_request_entry *entry;
	ssize_t ret = 0;

	if (count < sizeof(struct usb_ctrlrequest)) {
		pr_warn("Buffer too small for Control request\n");
		return -EINVAL;
	}

	if (file->f_flags & O_NONBLOCK) {
		if (list_empty(&g_pending_requests))
			return -EAGAIN;
	} else {
		ret = wait_event_interruptible(g_request_waitq,
			!list_empty(&g_pending_requests));
		if (ret)
			return ret;
	}

	mutex_lock(&g_request_lock);

	if (list_empty(&g_pending_requests)) {
		mutex_unlock(&g_request_lock);
		return 0;
	}

	entry = list_first_entry(&g_pending_requests,
		struct f_display_request_entry, list);

	if (copy_to_user(buf, &entry->ctrl, sizeof(entry->ctrl))) {
		mutex_unlock(&g_request_lock);
		pr_err("Failed to copy Control request to userspace\n");
		return -EFAULT;
	}

	list_del(&entry->list);
	kfree(entry);

	mutex_unlock(&g_request_lock);

	pr_debug("Display: Control request delivered to userspace\n");
	return sizeof(struct usb_ctrlrequest);
}

static ssize_t f_display_ctrl_write(struct file *file, const char __user *buf,
			size_t count, loff_t *ppos)
{
	struct ctrl_response response;
	struct usb_request *req;
	int ret = 0;

	if (!g_display_context) {
		pr_err("Display: No display context available\n");
		return -ENODEV;
	}

	if (count < sizeof(struct usb_ctrlrequest)) {
		pr_err("Display: Write buffer too small\n");
		return -EINVAL;
	}

	// copy response data from user space
	if (copy_from_user(&response, buf, min(count, sizeof(response)))) {
		pr_err("Display: Failed to copy response from userspace\n");
		return -EFAULT;
	}

	pr_debug("Display: Recv ctrl resp, data len: %d\n",
			response.data_length);

	mutex_lock(&g_response_lock);

	// get usb request
	req = g_display_context->cdev->req;
	if (!req) {
		pr_err("Display:No USB request available\n");
		ret = -ENODEV;
		goto out;
	}

	// setting response data
	if (response.data_length > 0) {
		memcpy(req->buf, response.data, response.data_length);
		req->length = response.data_length;
	} else {
		req->length = 0;
	}

	req->zero = 0;
	req->complete = NULL;

	// send response
	ret = usb_ep_queue(g_display_context->cdev->gadget->ep0,
					  req, GFP_KERNEL);
	if (ret < 0)
		pr_err("Failed to queue ctrl resp: %d\n", ret);
	else
		pr_debug("Display:ctrl queued success, length: %d, ret %d\n", req->length, ret);

out:
	mutex_unlock(&g_response_lock);
	return ret < 0 ? ret : count;
}

static unsigned int f_display_ctrl_poll(struct file *file, poll_table *wait)
{
	unsigned int mask = 0;

	poll_wait(file, &g_request_waitq, wait);

	mutex_lock(&g_request_lock);
	if (!list_empty(&g_pending_requests))
		mask |= POLLIN | POLLRDNORM;
	mutex_unlock(&g_request_lock);

	return mask;
}

static const struct file_operations f_ctrl_fops = {
	.owner	 = THIS_MODULE,
	.open	 = f_display_ctrl_open,
	.release = f_display_ctrl_release,
	.read	 = f_display_ctrl_read,
	.write	 = f_display_ctrl_write,
	.poll	 = f_display_ctrl_poll,
};

static int display_frame_alloc(struct f_display *display, struct usb_composite_dev *dev)
{
	int i;

	if (!display) {
		pr_err("%s no display\n", __func__);
		return -ENODEV;
	}

	if (!display->frame_buffer_dma.start) {
		display->frame_buffer_dma.size = PAGE_ALIGN(FRAME_BUFFER_SIZE);
		display->frame_buffer_dma.start =
				dma_alloc_attrs(dev->gadget->dev.parent,
					display->frame_buffer_dma.size,
					&display->frame_buffer_dma.start_dma,
					GFP_KERNEL, DMA_ATTR_WRITE_COMBINE);
		if (!display->frame_buffer_dma.start) {
			pr_err("Display: Failed to allocate frame_buffer_dma failed\n");
			return -ENOMEM;
		}
	}

	INIT_LIST_HEAD(&display->frame_list);
	INIT_LIST_HEAD(&display->empty_list);
	display->frame_count = 0;
	display->empty_count = 0;
	display->frame_buffer_offset = 0;
	init_waitqueue_head(&display->frame_waitq);

	for (i = 0; i < MAX_FRAMES_NUM; i++) {
		display->frames[i].data = NULL;
		display->frames[i].phy_addr = 0;
		display->frames[i].size = 0;
		display->frames[i].in_use = false;
		display->frames[i].id = i;
		INIT_LIST_HEAD(&display->frames[i].list);
		list_add_tail(&display->frames[i].list, &display->empty_list);
		display->empty_count++;
	}
	return 0;
}

static int display_frame_free(struct f_display *display, struct usb_composite_dev *dev)
{
	if (!display) {
		pr_err("Display: No display context available\n");
		return -ENODEV;
	}

	if (display->frame_buffer_dma.start) {
		dma_free_coherent(dev->gadget->dev.parent,
			display->frame_buffer_dma.size,
			display->frame_buffer_dma.start,
			display->frame_buffer_dma.start_dma);
	}

	display->frame_count = 0;
	display->cur_frame_size = 0;
	display->cur_frame_collected = 0;
	display->collecting_frame = false;

	return 0;
}

/* Bulk data device file operations */
static int f_display_bulk_open(struct inode *inode, struct file *file)
{
	return 0;
}

static int f_display_bulk_release(struct inode *inode, struct file *file)
{
	return 0;
}


static ssize_t f_display_bulk_read(struct file *file, char __user *buf,
			size_t count, loff_t *ppos)
{
	struct f_display *display = g_display_context;
	struct display_bulk_buffer *buffer;
	ssize_t ret = 0;
	unsigned long flags;
	size_t to_copy;

	if (!display) {
		pr_err("Display: No display context available\n");
		return -ENODEV;
	}

	if (file->f_flags & O_NONBLOCK) {
		spin_lock_irqsave(&display->bulk_lock, flags);
		if (list_empty(&display->bulk_list)) {
			spin_unlock_irqrestore(&display->bulk_lock, flags);
			return -EAGAIN;
		}
		spin_unlock_irqrestore(&display->bulk_lock, flags);
	} else {
		ret = wait_event_interruptible(display->bulk_waitq,
			!list_empty(&display->bulk_list));
		if (ret)
			return ret;
	}

	spin_lock_irqsave(&display->bulk_lock, flags);
	if (list_empty(&display->bulk_list)) {
		spin_unlock_irqrestore(&display->bulk_lock, flags);
		return -EAGAIN;
	}

	buffer = list_first_entry_or_null(&display->bulk_list,
				 struct display_bulk_buffer, list);
	if (!buffer) {
		spin_unlock_irqrestore(&display->bulk_lock, flags);
		return -EAGAIN;
	}
	if (!buffer->data) {
		spin_unlock_irqrestore(&display->bulk_lock, flags);
		return -EIO;
	}

	to_copy = min(count, buffer->len);
	if (copy_to_user(buf, buffer->data, to_copy)) {
		spin_unlock_irqrestore(&display->bulk_lock, flags);
		return -EFAULT;
	}

	if (to_copy == buffer->len) {
		list_del(&buffer->list);
		buffer->used = false;
		buffer->len = 0;
	} else {
		// Partial read - update buffer
		memmove(buffer->data, buffer->data + to_copy, buffer->len - to_copy);
		buffer->len -= to_copy;
	}
	spin_unlock_irqrestore(&display->bulk_lock, flags);

	pr_debug("Display: Delivered %zu bytes of bulk data to userspace\n", to_copy);
	return to_copy;
}

static ssize_t f_display_bulk_write(struct file *file, const char __user *buf,
			size_t count, loff_t *ppos)
{
	struct f_display *display = g_display_context;
	struct usb_request *req;
	struct usb_ep *ep;
	u8 *data_buf;
	int ret;

	if (!display) {
		pr_err("Display: No display context available\n");
		return -ENODEV;
	}

	if (!display->bulk_enabled) {
		pr_err("Display: Data endpoint not enabled\n");
		return -ENODEV;
	}

	if (count == 0)
		return 0;

	ep = display->port.in;
	if (!ep || !ep->desc) {
		pr_err("Display: Bulk IN endpoint not available\n");
		return -ENODEV;
	}

	data_buf = kmalloc(count, GFP_KERNEL);
	if (!data_buf)
		return -ENOMEM;

	if (copy_from_user(data_buf, buf, count)) {
		kfree(data_buf);
		return -EFAULT;
	}

	req = usb_ep_alloc_request(ep, GFP_KERNEL);
	if (!req) {
		kfree(data_buf);
		return -ENOMEM;
	}

	req->buf = data_buf;
	req->length = count;
	req->complete = display_bulk_in_complete;
	req->context = display;

	ret = usb_ep_queue(ep, req, GFP_KERNEL);
	if (ret < 0) {
		pr_err("Display: Failed to queue bulk in data: %d\n", ret);
		usb_ep_free_request(ep, req);
		kfree(data_buf);
		return ret;
	}

	pr_debug("Display: Sent %zu bytes via bulk in\n", count);
	return count;
}

static unsigned int f_display_bulk_poll(struct file *file, poll_table *wait)
{
	struct f_display *display = g_display_context;
	unsigned int mask = 0;

	if (!display)
		return POLLERR;

	poll_wait(file, &display->bulk_waitq, wait);

	spin_lock(&display->bulk_lock);
	if (!list_empty(&display->bulk_list))
		mask |= POLLIN | POLLRDNORM;
	spin_unlock(&display->bulk_lock);

	// Always writable if data is enabled for bulk out
	if (display->bulk_enabled && display->port.in && display->port.in->desc)
		mask |= POLLOUT | POLLWRNORM;

	return mask;
}

static long f_display_bulk_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct f_display *display = g_display_context;
	struct frame_buffer_info info = {0};
	struct frame_buffer *frame = NULL;
	long ret = 0;
	int val = 0;

	if (!display) {
		pr_err("Display: No display context available\n");
		return -ENODEV;
	}

	if (_IOC_TYPE(cmd) != DISP_IOC_MAGIC) {
		pr_err("Display: Invalid ioctl magic number\n");
		return -ENOTTY;
	}

	if (_IOC_NR(cmd) > DISP_IOC_MAXNR) {
		pr_err("Display: Invalid ioctl command number\n");
		return -ENOTTY;
	}

	if (_IOC_DIR(cmd) & _IOC_READ)
		ret = !access_ok((void __user *)arg, _IOC_SIZE(cmd));
	else if (_IOC_DIR(cmd) & _IOC_WRITE)
		ret = !access_ok((void __user *)arg, _IOC_SIZE(cmd));

	if (ret) {
		pr_err("Display: Invalid user pointer\n");
		return -EFAULT;
	}

	switch (cmd) {
	case DISP_DQ_FRAME:
		pr_debug("Display: DISP_GET_FRAME %d\n", display->frame_count);
		// Wait for available frame
		if (file->f_flags & O_NONBLOCK) {
			if (display->frame_count <= 0)
				return -EAGAIN;
		} else {
			ret = wait_event_interruptible(display->frame_waitq,
					display->frame_count > 0);
			if (ret)
				return ret;
		}
		spin_lock(&display->frame_lock);
		// Get the first available frame
		if (!list_empty(&display->frame_list)) {
			frame = list_first_entry(&display->frame_list, struct frame_buffer, list);
			if (frame == NULL) {
				spin_unlock(&display->frame_lock);
				pr_err("Display: Failed to get frame data\n");
				return -EFAULT;
			}

			info.size = frame->size;
			info.data = frame->data;
			info.offset = frame->offset;
			info.phy_addr = frame->phy_addr;
			info.id = frame->id;

			// Remove frame from list
			list_del(&frame->list);
			display->frame_count--;
			frame->in_use = false;
		} else {
			spin_unlock(&display->frame_lock);
			return -EAGAIN;
		}
		spin_unlock(&display->frame_lock);

		if (copy_to_user((void __user *)arg, &info, sizeof(info))) {
			// If copy failed, put frame back to empty list
			spin_lock(&display->frame_lock);
			if (frame) {
				list_add_tail(&frame->list, &display->empty_list);
				display->empty_count++;
			}
			spin_unlock(&display->frame_lock);
			pr_err("Display: Failed to copy frame info to userspace\n");
			return -EFAULT;
		}
		break;

	case DISP_Q_FRAME:
		if (copy_from_user(&info, (void __user *)arg, sizeof(info)))
			return -EFAULT;

		spin_lock(&display->frame_lock);
		frame = &display->frames[info.id];
		frame->in_use = false;
		frame->data = NULL;
		frame->size = 0;
		//INIT_LIST_HEAD(&frame->list);
		list_add_tail(&frame->list, &display->empty_list);
		display->empty_count++;
		spin_unlock(&display->frame_lock);
		break;

	case DISP_BUF_CNT:
		spin_lock(&display->frame_lock);
		val = display->frame_count;
		spin_unlock(&display->frame_lock);
		if (copy_to_user((void __user *)arg, &val, sizeof(int)))
			return -EFAULT;
		break;

	default:
		pr_err("Display: Unknown ioctl command\n");
		return -ENOTTY;
	}

	return 0;
}


static const struct file_operations f_bulk_fops = {
	.owner		= THIS_MODULE,
	.open		= f_display_bulk_open,
	.release	= f_display_bulk_release,
	.read		= f_display_bulk_read,
	.write		= f_display_bulk_write,
	.poll		= f_display_bulk_poll,
	.unlocked_ioctl = f_display_bulk_ioctl,
};

static void debug_config_descriptors(struct usb_descriptor_header **descriptors)
{
	int total_len = 0;
	int interface_count = 0;
	int endpoint_count = 0;

	while (*descriptors) {
		pr_debug("Display Desc: type=0x%02x, length=%d\n",
			   (*descriptors)->bDescriptorType,
			   (*descriptors)->bLength);

		total_len += (*descriptors)->bLength;

		if ((*descriptors)->bDescriptorType == USB_DT_INTERFACE) {
			struct usb_interface_descriptor *if_desc =
				(struct usb_interface_descriptor *)*descriptors;
			interface_count++;
			pr_debug("Display Intf %d: num_ep=%d, class=0x%02x\n",
				   if_desc->bInterfaceNumber,
				   if_desc->bNumEndpoints,
				   if_desc->bInterfaceClass);
		}

		if ((*descriptors)->bDescriptorType == USB_DT_ENDPOINT) {
			struct usb_endpoint_descriptor *ep_desc =
				(struct usb_endpoint_descriptor *)*descriptors;
			endpoint_count++;
			pr_debug("Display Ep: addr=0x%02x, attr=0x%02x\n",
				   ep_desc->bEndpointAddress,
				   ep_desc->bmAttributes);
		}

		descriptors++;
	}

	pr_info("Total length: %d, Intf: %d, ep_cnt: %d\n",
		   total_len, interface_count, endpoint_count);
}

static int display_handle_request(struct usb_composite_dev *cdev,
			const struct usb_ctrlrequest *ctrl,
			struct usb_request *req)
{
	struct f_display_request_entry *entry = NULL;

	pr_debug("Display: Control req: req=0x%02x\n", ctrl->bRequest);

	entry = kmalloc(sizeof(*entry), GFP_ATOMIC);
	if (!entry)
		return -ENOMEM;

	memcpy(&entry->ctrl, ctrl, sizeof(*ctrl));
	mutex_lock(&g_request_lock);
	list_add_tail(&entry->list, &g_pending_requests);
	mutex_unlock(&g_request_lock);
	wake_up(&g_request_waitq);

	pr_debug("Display:Control req queued\n");

	return 0;
}

static int display_setup(struct usb_function *f,
			const struct usb_ctrlrequest *ctrl)
{
	struct f_display	*display = func_to_display(f);
	struct usb_composite_dev *cdev = f->config->cdev;
	struct usb_request	*req = cdev->req;
	int			value = -EOPNOTSUPP;

	display->cdev = cdev;

	if ((ctrl->bRequestType & USB_TYPE_MASK) == USB_TYPE_STANDARD ||
		(ctrl->bRequestType & USB_TYPE_MASK) == USB_TYPE_VENDOR) {
		value = display_handle_request(cdev, ctrl, req);
		if (value != 0) {
			pr_debug("Control req handled internal, val=%d\n", value);
			goto respond;
		}
		return 0;
	}

	if (ctrl->bRequestType & USB_TYPE_MASK) {
		pr_info("Display: Class request not supported\n");
		value = -EOPNOTSUPP;
	}

respond:
	if (value >= 0) {
		req->zero = 0;
		req->length = value;
		if (usb_ep_queue(cdev->gadget->ep0, req, GFP_ATOMIC) < 0)
			ERROR(cdev, "display ep0 queue failed: %d\n", value);
		else
			pr_debug("Display setup response queued successfully\n");
	} else {
		pr_warn("Display setup request not handled: %d\n", value);
	}
	/* device either stalls (value < 0) or reports success */
	return value;
}

static int display_set_alt(struct usb_function *f,
			unsigned int intf, unsigned int alt)
{
	struct f_display	*display = func_to_display(f);
	struct usb_composite_dev *cdev = f->config->cdev;
	struct usb_request *req;
	int i, ret;

	pr_debug("Display set_alt  CALLED: intf=%u, alt=%u !!!\n", intf, alt);

	if (intf == display->data_id) {
		pr_debug("Display:Setting alternate for data intf %d\n", intf);

		if (!display->port.in->desc || !display->port.out->desc) {
			pr_debug("Display:Activating display data endpoints\n");
			if (config_ep_by_speed(cdev->gadget,
					f, display->port.in) ||
				config_ep_by_speed(cdev->gadget,
					f, display->port.out)) {
				display->port.in->desc = NULL;
				display->port.out->desc = NULL;
				pr_err("Display:Failed to config data endpoints\n");
				return -EINVAL;
			}
		}

		usb_ep_enable(display->port.in);
		usb_ep_enable(display->port.out);

		// Start bulk out data reception
		display->num_bulk_out_reqs = 0;
		if (display->port.out->desc) {
			for (i = 0; i < MAX_BULK_OUT_REQUESTS; i++) { // Allocate 2 requests
				req = usb_ep_alloc_request(display->port.out, GFP_KERNEL);
				if (!req) {
					pr_err("Display:Failed to allocate bulk out request\n");
					continue;
				}

				req->buf = kmalloc(display->port.out->maxpacket, GFP_KERNEL);
				if (!req->buf) {
					usb_ep_free_request(display->port.out, req);
					continue;
				}

				req->length = display->port.out->maxpacket;
				req->complete = display_bulk_out_complete;
				req->context = display;

				ret = usb_ep_queue(display->port.out, req, GFP_KERNEL);
				if (ret < 0) {
					pr_err("Display:Failed to queue bulk out req: %d\n", ret);
					kfree(req->buf);
					usb_ep_free_request(display->port.out, req);
				} else {
					display->bulk_out_reqs[display->num_bulk_out_reqs++] = req;
				}
			}
		}

		display->bulk_enabled = true;
		pr_debug("Display:Data interface enabled with %d bulk req\n",
			display->num_bulk_out_reqs);

	} else {
		pr_err("Invalid interface number: %d\n", intf);
		return -EINVAL;
	}

	pr_debug("Display set_alt completed successfully\n");
	return 0;
}

static void display_disable(struct usb_function *f)
{
	struct f_display	*display = func_to_display(f);
	unsigned long	flags;
	int		i;

	// Disable data forwarding
	spin_lock_irqsave(&display->bulk_lock, flags);
	display->bulk_enabled = false;
	spin_unlock_irqrestore(&display->bulk_lock, flags);

	// Cancel all pending bulk out requests
	for (i = 0; i < display->num_bulk_out_reqs; i++) {
		if (display->bulk_out_reqs[i])
			usb_ep_dequeue(display->port.out, display->bulk_out_reqs[i]);
	}
	display->num_bulk_out_reqs = 0;
	if (display->port.in->enabled)
		usb_ep_disable(display->port.in);
	if (display->port.out->enabled)
		usb_ep_disable(display->port.out);
}

/* display function driver setup/binding */
static int display_bind(struct usb_configuration *c, struct usb_function *f)
{
	struct usb_composite_dev *cdev = c->cdev;
	struct f_display	*display = func_to_display(f);
	struct usb_string	*us;
	struct usb_ep		*ep;
	int			status;
	int			i;

	g_display_context = NULL;

	INIT_LIST_HEAD(&display->bulk_list);
	init_waitqueue_head(&display->bulk_waitq);
	spin_lock_init(&display->bulk_lock);
	display->bulk_enabled = false;
	display->num_bulk_out_reqs = 0;
	memset(display->bulk_out_reqs, 0, sizeof(display->bulk_out_reqs));

	display->bulk_buffer_pool = kmalloc_array(BULK_BUFFER_NUM,
				sizeof(struct display_bulk_buffer), GFP_KERNEL);
	if (!display->bulk_buffer_pool)
		return -ENOMEM;

	display->next_buffer_idx = 0;
	for (i = 0; i < BULK_BUFFER_NUM; i++) {
		INIT_LIST_HEAD(&display->bulk_buffer_pool[i].list);
		display->bulk_buffer_pool[i].data = kmalloc(BULK_BUFFER_SIZE, GFP_KERNEL);
		if (!display->bulk_buffer_pool[i].data) {
			while (i-- > 0)
				kfree(display->bulk_buffer_pool[i].data);

			status = -ENOMEM;
			goto fail;
		}
		display->bulk_buffer_pool[i].used = false;
		display->bulk_buffer_pool[i].len = 0;
	}


	init_completion(&g_response_completion);

	us = usb_gstrings_attach(cdev, display_strings,
			ARRAY_SIZE(display_string_defs));
	if (IS_ERR(us))
		return PTR_ERR(us);

	display_data_interface_desc.iInterface = us[DISPLAY_DATA_IDX].id;

	/* allocate data interface id */
	status = usb_interface_id(c, f);
	if (status < 0)
		goto fail;
	display->data_id = status;
	display_data_interface_desc.bInterfaceNumber = status;

	status = -ENODEV;

	ep = usb_ep_autoconfig(cdev->gadget, &display_fs_in_desc);
	if (!ep) {
		pr_err("Failed to allocate IN endpoint\n");
		goto fail;
	}
	display->port.in = ep;

	ep = usb_ep_autoconfig(cdev->gadget, &display_fs_out_desc);
	if (!ep) {
		pr_err("Failed to allocate OUT endpoint\n");
		goto fail;
	}
	display->port.out = ep;

	display_hs_in_desc.bEndpointAddress =
		display_fs_in_desc.bEndpointAddress;
	display_hs_out_desc.bEndpointAddress =
		display_fs_out_desc.bEndpointAddress;

	debug_config_descriptors(display_fs_function);

	status = usb_assign_descriptors(f, display_fs_function,
		display_hs_function, NULL, NULL);
	if (status) {
		pr_err("Failed to assign descriptors: %d\n", status);
		goto fail;
	}

	if (display_frame_alloc(display, cdev)) {
		status = -ENOMEM;
		goto fail;
	}
	g_display_context = display;
	pr_debug("Display: bind success, EP - IN:%s OUT:%s\n",
		display->port.in->name, display->port.out->name);
	return 0;

fail:

	kfree(display->bulk_buffer_pool);
	display->bulk_buffer_pool = NULL;

	ERROR(cdev, "%s/%p: can't bind, err %d\n", f->name, f, status);

	return status;
}

static void display_unbind(struct usb_configuration *c, struct usb_function *f)
{
	struct f_display *display = func_to_display(f);
	struct display_bulk_buffer *buffer, *tmp;
	struct f_display_request_entry *entry, *tmp_entry;
	unsigned long flags;
	int i;

	pr_debug("Display unbind\n");
	mutex_lock(&g_response_lock);
	g_display_context = NULL;
	mutex_unlock(&g_response_lock);

	// Release frame buffer
	display_frame_free(display, c->cdev);

	// Clean up data buffers
	spin_lock_irqsave(&display->bulk_lock, flags);
	list_for_each_entry_safe(buffer, tmp, &display->bulk_list, list) {
		list_del(&buffer->list);
	}
	spin_unlock_irqrestore(&display->bulk_lock, flags);

	if (display->bulk_buffer_pool) {
		for (i = 0; i < BULK_BUFFER_NUM; i++)
			kfree(display->bulk_buffer_pool[i].data);
		kfree(display->bulk_buffer_pool);
	}

	// Clean up bulk requests
	for (i = 0; i < display->num_bulk_out_reqs; i++) {
		if (display->bulk_out_reqs[i]) {
			kfree(display->bulk_out_reqs[i]->buf);
			usb_ep_free_request(display->port.out, display->bulk_out_reqs[i]);
		}
	}
	display->num_bulk_out_reqs = 0;

	// Clean up Control requests
	mutex_lock(&g_request_lock);
	list_for_each_entry_safe(entry, tmp_entry,
			&g_pending_requests, list) {
		list_del(&entry->list);
		kfree(entry);
	}
	mutex_unlock(&g_request_lock);

	display_string_defs[0].id = 0;
	usb_free_all_descriptors(f);
}

static bool display_req_match(struct usb_function *f,
			    const struct usb_ctrlrequest *ctrl,
			    bool configured)
{
	if ((ctrl->bRequestType & USB_TYPE_MASK) == USB_TYPE_VENDOR) {
		pr_debug("display req_match over: bRequestType %x, %x",
			ctrl->bRequestType, USB_TYPE_VENDOR);
		return true;
	}

	return false;
}

static void display_free_func(struct usb_function *f)
{
	struct f_display *display = func_to_display(f);

	kfree(display);
}

static int __init display_device_init(void)
{
	int ret;

	INIT_LIST_HEAD(&g_pending_requests);
	init_waitqueue_head(&g_request_waitq);

	// Register Control request device
	g_ctrl_miscdev.minor = MISC_DYNAMIC_MINOR;
	g_ctrl_miscdev.name = "usb-disp0";
	g_ctrl_miscdev.fops = &f_ctrl_fops;
	g_ctrl_miscdev.mode = 0666;

	ret = misc_register(&g_ctrl_miscdev);
	if (ret) {
		pr_err("Display:Failed to register ctrl dev: %d\n", ret);
		return ret;
	}

	// Register bulk data device
	g_bulk_miscdev.minor = MISC_DYNAMIC_MINOR;
	g_bulk_miscdev.name = "usb-disp1";
	g_bulk_miscdev.fops = &f_bulk_fops;
	g_bulk_miscdev.mode = 0666;

	ret = misc_register(&g_bulk_miscdev);
	if (ret) {
		pr_err("Display: Failed to register data dev: %d\n", ret);
		misc_deregister(&g_ctrl_miscdev);
		return ret;
	}

	pr_info("Display dev registered: /dev/%s (bulk)\n",
		g_bulk_miscdev.name);
	return 0;
}

static void __exit display_device_exit(void)
{
	struct f_display_request_entry *entry, *tmp;

	mutex_lock(&g_request_lock);
	list_for_each_entry_safe(entry, tmp,
			&g_pending_requests, list) {
		list_del(&entry->list);
		kfree(entry);
	}
	mutex_unlock(&g_request_lock);

	misc_deregister(&g_bulk_miscdev);
	misc_deregister(&g_ctrl_miscdev);
	pr_info("Display devices unregistered\n");
}

static struct usb_function *display_alloc_func(struct usb_function_instance *fi)
{
	struct f_display_opts *opts;
	struct f_display *display;

	display = kzalloc(sizeof(*display), GFP_KERNEL);
	if (!display)
		return ERR_PTR(-ENOMEM);

	opts = container_of(fi, struct f_display_opts, func_inst);

	display->port.func.name = "display";
	display->port.func.strings = display_strings;
	display->port.func.bind = display_bind;
	display->port.func.set_alt = display_set_alt;
	display->port.func.setup = display_setup;
	display->port.func.disable = display_disable;
	display->port.func.unbind = display_unbind;
	display->port.func.req_match = display_req_match;
	display->port.func.free_func = display_free_func;

	display_device_init();

	return &display->port.func;
}

static inline struct f_display_opts *to_f_display_opts(struct config_item *item)
{
	return container_of(to_config_group(item), struct f_display_opts,
			func_inst.group);
}

static void display_attr_release(struct config_item *item)
{
	struct f_display_opts *opts = to_f_display_opts(item);

	usb_put_function_instance(&opts->func_inst);
}

static struct configfs_item_operations display_item_ops = {
	.release	= display_attr_release,
};

static struct configfs_attribute *display_attrs[] = {
	NULL,
};

static const struct config_item_type display_func_type = {
	.ct_item_ops	= &display_item_ops,
	.ct_attrs	= display_attrs,
	.ct_owner	= THIS_MODULE,
};

static void display_free_instance(struct usb_function_instance *fi)
{
	struct f_display_opts *opts;

	display_device_exit();
	opts = container_of(fi, struct f_display_opts, func_inst);
	pr_debug("Display instance freed\n");
	kfree(opts);
}

static struct usb_function_instance *display_alloc_instance(void)
{
	struct f_display_opts *opts;

	opts = kzalloc(sizeof(*opts), GFP_KERNEL);
	if (!opts)
		return ERR_PTR(-ENOMEM);
	opts->func_inst.free_func_inst = display_free_instance;

	config_group_init_type_name(&opts->func_inst.group, "",
			&display_func_type);
	pr_debug("Display instance allocated\n");
	return &opts->func_inst;
}

DECLARE_USB_FUNCTION_INIT(display, display_alloc_instance, display_alloc_func);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("ArtInChip");
