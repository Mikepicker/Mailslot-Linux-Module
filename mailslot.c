/* Mailslot implementation using Kernel Module */

#define EXPORT_SYMTAB
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/sched.h>	
#include <linux/pid.h>		/* For pid types */
#include <linux/version.h>	/* For LINUX_VERSION_CODE */
#include <linux/slab.h>		/* For kmalloc */
#include <asm/uaccess.h>	/* For copy_to_user */
#include <linux/cdev.h>		/* Modern way to handle cdevs (cdev_alloc())*/

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Michele Rullo");

/* ioctl operations */
static int mailslot_open(struct inode *, struct file *);
static int mailslot_release(struct inode *, struct file *);
static ssize_t mailslot_read(struct file * filp, const char * buf, size_t, loff_t *);
static ssize_t mailslot_write(struct file * filp, const char * buf, size_t, loff_t *);


#define DEVICE_NAME "mailslot"
#define INSTANCES 256
#define MESSAGE_SIZE 256
#define MAILSLOT_STORAGE 256
#define MINOR_LOWER 0

static int Major;            /* Major number assigned to broadcast device driver */
struct cdev *mailslot_cdev;

// Message Struct
static struct message
{
	char *content;
	int len;
};

// Mailslot instance struct
static struct mailslot
{
	int opened;	// 1 if opened
	struct message *messages[MAILSLOT_STORAGE];
	int messages_count;
};


/* Module facilities */
static int pushMessage(const char *buff, int len, int instance);
static int getMessage(const char *buff, int instance);
static int clearMailslot(int instance);

// Mailslots
static struct mailslot* instances[INSTANCES];
static int instances_count = 0;

/* Create a new mailslot instance */
static int mailslot_open(struct inode *inode, struct file *file)
{
	printk("Opening mailslot\n");

	if (instances_count == INSTANCES)
	{
		printk("No more room to allocate new mailslot\n");
		return -1;
	}

	// Get minor number (device)
	int minor = iminor(file->f_path.dentry->d_inode);

	if (!instances[minor]->opened)
	{
		instances_count++;
		instances[minor]->opened = 1;
		printk("New mailslot instance created with minor number: %d. There are %d mailslots now.\n", minor, instances_count);
		return 0;
	}
	else
	{
		printk("There is already a mailslot opened associated with minor number %d.\n", minor);
		return -1;
	}
}	


static int mailslot_release(struct inode *inode, struct file *file)
{
	printk("Releasing mailslot\n");

	/* This should not happen */
	if (instances_count == 0)
	{
		printk("No mailslots no remove!\n");
		return -1;
	}

	// Get minor number (device)
	int minor = iminor(file->f_path.dentry->d_inode);

	// Release mailslot if it's opened
	if (instances[minor]->opened)
	{
		instances_count--;
		instances[minor]->opened = 0;
		printk("Successfully closed mailslot with minor number: %d\n",minor);
		return 0;
	}
	else
	{
		printk("Mailslot %d is already closed.\n", minor);
		return -1;
	}
	

}

/* Read from desired mailslot. Each message gets removed when consumed.*/
static ssize_t mailslot_read(struct file *filp,
   const char *buff,
   size_t len,
   loff_t *off)
{
	printk("Mailslot read\n");

	// 1. Get minor number
	int minor = iminor(filp->f_path.dentry->d_inode);
	
	// 2. Get message
	char *ret_buff;
	getMessage(buff,minor);

	printk("Message: %s\n",buff);

	// Change reading pos
	if (*off == 0)
	{
		*off += 1;
		return *off;
	}
	else
		return 0;
}

/* Write on desired mailslot */
static ssize_t mailslot_write(struct file *filp,
   const char *buff,
   size_t len,
   loff_t *off)
{
	printk("Mailslot writing %d bytes\n",len);
	
	// 1. Get minor number (device)
	int minor = iminor(filp->f_path.dentry->d_inode);
	
	// 2. Push message to mailslot
	pushMessage(buff,len,minor);

	printk("Done!\n");
	return len;
}

/* Module facilities */

// Push message into mailslot (specified by "instance")
static int pushMessage(const char *buff, int len, int instance)
{
	printk("Message content: %s\n", buff);
	printk("Pushing message to mailslot: %d\n",instance);
	int *count = &instances[instance]->messages_count;

	// 1. Check if there's space
	if (*count == MAILSLOT_STORAGE)
	{
		printk("Mailslot full, message discarded\n");
		return -1;
	}
	
	// 2. Copy input message to allocated space
	strcpy(instances[instance]->messages[*count]->content,buff);
	instances[instance]->messages[*count]->len = len;
	
	printk("Message pushed: %s\n",instances[instance]->messages[*count]->content);
	printk("Message length: %d\n", instances[instance]->messages[*count]->len);
	// 3. Increment message counter
	*count += 1;
	
	printk("There are currently %d messages in this mailslot\n",*count);
	
	return 0;
}

// Get message (FIFO order). Once a message is returned is also removed from its mailslot
static int getMessage(const char *buff, int instance)
{
	int *count = &instances[instance]->messages_count;
	if (*count == 0)
	{
		printk("No message to read\n");
		return 0;
	}

	struct message *msg = instances[instance]->messages[*count-1];

	// 1. Copy message to return struct
	printk("Copying message: %s\n", msg->content);
	printk("Message length: %d\n", msg->len);
	//copy_to_user(buff,msg->content,msg->len);
	strcpy(buff,msg->content);

	// 2. Decrease message counter
	*count--;

	printk("Message successfully returned and removed\n");
	return 0;
}

// Clear mailslot (called by "release" ioctl operation)
static int clearMailslot(int instance)
{
	printk("Mailslot %d cleared\n", instance);
	return 0;
}

// File operations struct
static struct file_operations fops =
{
	.read = mailslot_read,
	.write = mailslot_write,
	.open =  mailslot_open,
	.release = mailslot_release
};

int init_module(void)
{
	// 1. Allocate dynamically a device numbers region
	dev_t dev;
	int err = alloc_chrdev_region(&dev, MINOR_LOWER, MINOR_LOWER+INSTANCES, DEVICE_NAME);

	if (err)
	{
		printk("Allocating chrdev region failed\n");
		return err;
	}

	Major = MAJOR(dev);

	// 2. Allocate cdev struct
	mailslot_cdev = cdev_alloc();

	// 3. Init cdev
	cdev_init(mailslot_cdev, &fops);

	// 4. Add cdev
	err = cdev_add(mailslot_cdev, dev, MINOR_LOWER+INSTANCES);

	if (err)
	{
		printk("Adding cdev failed\n");
		return err;
	}

	// Old way:
	// Major = register_chrdev(MINOR_LOWER, DEVICE_NAME, &fops);

	if (Major < 0) 
	{
		printk("Registering mailslot device failed\n");
		return Major;
	}

	printk(KERN_INFO "Mailslot device registered, it is assigned major number %d\n", Major);

	// 5. Allocate mailslot instances
	int i;
	for (i = 0; i < INSTANCES; i++)
	{
		instances[i] = kmalloc(sizeof(struct mailslot), GFP_KERNEL);
		if (!instances[i])
		{
			printk("Allocating memory for mailslot failed\n");
			return -1;
		}
		memset(instances[i], 0, sizeof(struct mailslot));
		instances[i]->opened = 0;
		instances[i]->messages_count = 0;

		// 6. Allocate space for messages
		int j;
		for (j = 0; j < MAILSLOT_STORAGE; j++)
		{
			instances[i]->messages[j] = kmalloc(sizeof(struct message), GFP_KERNEL);
			if (!instances[i]->messages[j])
			{
				printk("Allocating memory for message failed\n");
				return -1;
			}

			instances[i]->messages[j]->content = kmalloc(sizeof(char)*MESSAGE_SIZE,GFP_KERNEL);
			if (!instances[i]->messages[j]->content)
			{
				printk("Allocating memory for message content failed\n");
			}
		}
	}
	return 0;
}

void cleanup_module(void)
{
	printk("Cleaning Mailslot Module Up\n");

	// De-Allocate memory for mailslots
	int i;
	for (i = 0; i < INSTANCES; i++)
	{
		int j;
		for (j = 0; j < MAILSLOT_STORAGE; j++)
		{
			kfree(instances[i]->messages[j]->content);
			kfree(instances[i]->messages[j]);
		}

		kfree(instances[i]);
	}

	unregister_chrdev(Major, DEVICE_NAME);
	cdev_del(mailslot_cdev);

	printk(KERN_INFO "Mailslot device unregistered, it was assigned major number %d\n", Major);
}

