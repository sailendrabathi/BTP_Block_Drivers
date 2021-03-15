#include <linux/version.h> 	/* LINUX_VERSION_CODE  */
#include <linux/blk-mq.h>	
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/kernel.h>	/* printk() */
#include <linux/slab.h>		/* kmalloc() */
#include <linux/fs.h>		/* everything... */
#include <linux/errno.h>	/* error codes */
#include <linux/timer.h>
#include <linux/types.h>	/* size_t */
#include <linux/fcntl.h>	/* O_ACCMODE */
#include <linux/hdreg.h>	/* HDIO_GETGEO */
#include <linux/kdev_t.h>
#include <linux/vmalloc.h>
#include <linux/genhd.h>
#include <linux/blkdev.h>
#include <linux/buffer_head.h>	/* invalidate_bdev */
#include <linux/bio.h>
#include <linux/uio.h>
#include <linux/file.h>
#include <linux/kthread.h>
#include <linux/wait.h>
#include <linux/semaphore.h>
#include "utils.h"

MODULE_LICENSE("Dual BSD/GPL");

static int sbull_major = 0;
module_param(sbull_major, int, 0);
static int hardsect_size = 512;
module_param(hardsect_size, int, 0);
static int nsectors = 102400;	/* How big the drive is (~50M)*/
module_param(nsectors, int, 0);
static int ndevices = 1;
module_param(ndevices, int, 0);


#define SBULL_MINORS	16
#define MINOR_SHIFT	4
#define DEVNUM(kdevnum)	(MINOR(kdev_t_to_nr(kdevnum)) >> MINOR_SHIFT
#define KERNEL_SECTOR_SIZE	512

#define CHUNK_IN_SECTORS 8

#define NUM_DISKS 3

#define INVALIDATE_DELAY	30*HZ

/*
 * The internal representation of our device.
 */
struct sbull_dev {
	int size;                       /* Device size in sectors */
	u8 *data;                       /* The data array */
	short users;                    /* How many users */
	short media_change;             /* Flag a media change? */
	spinlock_t lock;                /* For mutual exclusion */
	struct blk_mq_tag_set tag_set;	/* tag_set added */
	struct request_queue *queue;    /* The device request queue */
	struct gendisk *gd;             /* The gendisk structure */
	struct timer_list timer;        /* For simulated media changes */
	struct disk_dev disk_devs[NUM_DISKS];
};

static struct sbull_dev *Devices = NULL;

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 9, 0))
static inline struct request_queue *
blk_generic_alloc_queue(make_request_fn make_request, int node_id)
#else
static inline struct request_queue *
blk_generic_alloc_queue(int node_id)
#endif
{
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 7, 0))
	struct request_queue *q = blk_alloc_queue(GFP_KERNEL);
	if (q != NULL)
		blk_queue_make_request(q, make_request);

	return (q);
#elif (LINUX_VERSION_CODE < KERNEL_VERSION(5, 9, 0))
	return (blk_alloc_queue(make_request, node_id));
#else
	return (blk_alloc_queue(node_id));
#endif
}

static int lo_write_bvec(struct file *file, struct bio_vec *bvec, loff_t *ppos)
{
	struct iov_iter i;
	ssize_t bw;
	iov_iter_bvec(&i, WRITE, bvec, 1, bvec->bv_len);
	file_start_write(file);
	bw = vfs_iter_write(file, &i, ppos, 0);
	file_end_write(file);

	if (likely(bw ==  bvec->bv_len))
		return 0;

	printk_ratelimited(KERN_ERR
		"loop: Write error at byte offset %llu, length %i.\n",
		(unsigned long long)*ppos, bvec->bv_len);
	if (bw >= 0)
		bw = -EIO;
	return bw;
}
/*
 * Transfer a single BIO.
 */
static int sbull_xfer_bio(struct file * backing_file, struct bio *bio)
{
	
	// printk(KERN_INFO "sbull_xfer_bio");
	struct bio_vec bvec;
	struct bvec_iter iter;
	struct iov_iter i;
	ssize_t len;
	loff_t pos = ((bio->bi_iter.bi_sector)*KERNEL_SECTOR_SIZE); //((loff_t)blk_rq_pos(rq) << 9);
	if(bio_data_dir(bio) == WRITE){
		bio_for_each_segment(bvec, bio, iter)
		{
			len = lo_write_bvec(backing_file,&bvec,&pos);
			if (len < 0)
				return len;
			cond_resched();
		}
	}
	else {
		bio_for_each_segment(bvec, bio, iter)
		{
			iov_iter_bvec(&i, READ, &bvec, 1, bvec.bv_len);
			len = vfs_iter_read(backing_file, &i, &pos, 0);
			if (len < 0)
				return len;
			cond_resched();
		}
	}	

	return 0; /* Always "succeed" */
}

DEFINE_SPINLOCK(lock3);
struct semaphore barrier =  __SEMAPHORE_INITIALIZER(barrier, 0);
int active_tasks = 0;


struct par_list my_list;
wait_queue_head_t par_req_event;
spinlock_t par_lock;

static int thread_test(void *data){
	struct bio* bio;
	struct disk_dev* ddev = (struct disk_dev*)data;
	struct my_bio *mb;
	while(!kthread_should_stop()) {
		wait_event_interruptible(ddev->req_event,kthread_should_stop() || !my_bio_list_empty(&(ddev->my_bio_list)));
		spin_lock(&(ddev->lock));
        if (my_bio_list_empty(&(ddev->my_bio_list)))
        {
            spin_unlock(&(ddev->lock));
            continue;
        }
		mb = my_bio_list_pop(&(ddev->my_bio_list));
		bio = mb->bio;

		if(bio_data_dir(bio) == WRITE){
			struct bio_vec bvec;
			struct bvec_iter iter;
			loff_t pos = ((bio->bi_iter.bi_sector)*KERNEL_SECTOR_SIZE);
			ssize_t len;
			bio_for_each_segment(bvec,bio,iter){
				char newbuf[bvec.bv_len+1];
				char *written = kmap_atomic(bvec.bv_page);
				char *buffstart = written + bvec.bv_offset;
				unsigned int k;
				for(k=0; k<bvec.bv_len; k++)
					newbuf[k] = buffstart[k];
				kunmap_atomic(written);
				char oldbuf[bvec.bv_len+1];
				loff_t temppos = pos;
				len = kernel_read(ddev->backing_file , (void __user *) oldbuf ,bvec.bv_len , &temppos);
				if(len < 0) printk(KERN_INFO"Parity: kernel_read failed");
				for(k=0; k<bvec.bv_len; k++)
					newbuf[k] = newbuf[k]^oldbuf[k];
				struct par_dev *pdev = kmalloc(sizeof(struct par_dev), GFP_KERNEL);
				pdev->diskbuf = newbuf;
				pdev->pos = pos;
				pdev->disk_num = mb->disk_num;
				pdev->parity_disk = mb->parity_disk;
				pdev->bv_len = bvec.bv_len;
				spin_lock(&par_lock);
				par_list_add(&my_list,pdev);
				wake_up(&par_req_event);
				spin_unlock(&par_lock);
			}
		}

		spin_unlock(&(ddev->lock));
		sbull_xfer_bio(ddev->backing_file, bio);
		bio_put(bio);
		kfree(mb);
		spin_lock(&lock3);
		active_tasks--;
		if(active_tasks == 0) up(&barrier);
		spin_unlock(&lock3);
	}
	return 0;
}


static int parity_thread(void *data){
	struct par_dev *pdev;
	struct sbull_dev* dev = (struct sbull_dev*)data;
	while(!kthread_should_stop()) {
		wait_event_interruptible(par_req_event,kthread_should_stop() || !par_list_empty(&(my_list)));
		spin_lock(&(par_lock));
        if (par_list_empty(&(my_list)))
        {
            spin_unlock(&(par_lock));
            continue;
        }
		pdev = par_list_pop(&(my_list));
		spin_unlock(&(par_lock));

		char parbuf[pdev->bv_len+1];
		loff_t temppos1 = pdev->pos;
		ssize_t len;
		len = kernel_read(dev->disk_devs[pdev->parity_disk].backing_file , (void __user *) parbuf ,pdev->bv_len , &temppos1);
		if(len < 0) printk(KERN_INFO"Parity: kernel_read failed");
		int k;
		for(k=0; k<pdev->bv_len; k++)
			parbuf[k] = parbuf[k]^pdev->diskbuf[k];
		loff_t pos = pdev->pos;
		printk(KERN_NOTICE "sbull_raid: parity check\n");
		len = kernel_write(dev->disk_devs[pdev->parity_disk].backing_file , (void __user *) parbuf, pdev->bv_len , &pos);
		if(len<0) printk(KERN_INFO "Parity : kernel_write failed");
		kfree(pdev);
	}
	return 0;
}


/*
 * Transfer a full request.
 */
static int sbull_xfer_request(struct sbull_dev *dev, struct request *req)
{
	struct bio *bio;
	int nsect = 0;
    
	printk(KERN_INFO "sbull_xfer_request");

	__rq_for_each_bio(bio, req){
		struct bio* bio_1 = bio_clone_fast(bio, GFP_NOIO, NULL);

		unsigned int start_sector = bio_1->bi_iter.bi_sector;
		unsigned int rem = bio_1->bi_iter.bi_size / KERNEL_SECTOR_SIZE;
			
		while(rem > 0){
			unsigned int chunk_index = start_sector / CHUNK_IN_SECTORS;
			unsigned int stripe_index = chunk_index / (NUM_DISKS-1);
			unsigned int disk_num = (start_sector % (CHUNK_IN_SECTORS * (NUM_DISKS-1))) / CHUNK_IN_SECTORS;
			unsigned int sector_num = start_sector;

			unsigned int parity_disk = (NUM_DISKS-1) - ((stripe_index/4)%NUM_DISKS);
			
			sector_num /= CHUNK_IN_SECTORS * (NUM_DISKS-1);
			sector_num *= CHUNK_IN_SECTORS;
			sector_num += start_sector % CHUNK_IN_SECTORS;
			unsigned int n_sec = CHUNK_IN_SECTORS - (sector_num % CHUNK_IN_SECTORS);
			if(parity_disk <= disk_num) disk_num++;

			struct my_bio* mb = kmalloc(sizeof(struct my_bio), GFP_KERNEL);
			mb->disk_num = disk_num;
			mb->parity_disk = parity_disk;
			if(rem <= n_sec){
				n_sec = rem;
				bio_1->bi_iter.bi_sector = sector_num;
				mb->bio = bio_1;
			}
			else{
				struct bio *bio_temp = bio_split(bio_1, n_sec, GFP_NOIO, NULL);
				bio_temp->bi_iter.bi_sector = sector_num;
				mb->bio = bio_temp;
			}
			
			spin_lock(&(dev->disk_devs[disk_num].lock));
			my_bio_list_add(&(dev->disk_devs[disk_num].my_bio_list), mb);
			wake_up(&dev->disk_devs[disk_num].req_event);
			spin_unlock(&(dev->disk_devs[disk_num].lock));

			spin_lock(&lock3);
			active_tasks++;
			spin_unlock(&lock3);

			start_sector += n_sec;
			rem -= n_sec;
			nsect += n_sec;
		}
	}
	down(&barrier);


	return nsect;
}



/*
 * Smarter request function that "handles clustering".
 */
static blk_status_t sbull_full_request(struct blk_mq_hw_ctx * hctx, const struct blk_mq_queue_data * bd)
{
	struct request *req = bd->rq;
	int sectors_xferred;
	struct sbull_dev *dev = req->q->queuedata;
	blk_status_t  ret;


	printk(KERN_INFO "sbull_full_request");


	blk_mq_start_request (req);
		if (blk_rq_is_passthrough(req)) {
			printk (KERN_NOTICE "Skip non-fs request\n");
			
			ret = BLK_STS_IOERR; //-EIO;
			goto done;
		}
		sectors_xferred = sbull_xfer_request(dev, req);
		ret = BLK_STS_OK; 
	done:
		
		blk_mq_end_request (req, ret);
	//}
	return ret;
}

/*
 * Open and close.
 */

static int sbull_open(struct block_device *bdev, fmode_t mode)
{
	printk(KERN_INFO "file_sbull: entered sbull_open.\n");
	struct sbull_dev *dev = bdev->bd_disk->private_data;

	del_timer_sync(&dev->timer);
	//filp->private_data = dev;
	spin_lock(&dev->lock);
	if (! dev->users) 
	{
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))
		check_disk_change(bdev);
#else
                /* For newer kernels (as of 5.10), bdev_check_media_change()
                 * is used, in favor of check_disk_change(),
                 * with the modification that invalidation
                 * is no longer forced. */

                if(bdev_check_media_change(bdev))
                {
                        struct gendisk *gd = bdev->bd_disk;
                        const struct block_device_operations *bdo = gd->fops;
                        if (bdo && bdo->revalidate_disk)
                                bdo->revalidate_disk(gd);
                }
#endif
	}
	dev->users++;
	spin_unlock(&dev->lock);
	return 0;
}

static void sbull_release(struct gendisk *disk, fmode_t mode)
{
	struct sbull_dev *dev = disk->private_data;

	spin_lock(&dev->lock);
	dev->users--;

	if (!dev->users) {
		dev->timer.expires = jiffies + INVALIDATE_DELAY;
		add_timer(&dev->timer);
	}
	spin_unlock(&dev->lock);
}

/*
 * Look for a (simulated) media change.
 */
int sbull_media_changed(struct gendisk *gd)
{
	struct sbull_dev *dev = gd->private_data;
	
	return dev->media_change;
}

/*
 * Revalidate.  WE DO NOT TAKE THE LOCK HERE, for fear of deadlocking
 * with open.  That needs to be reevaluated.
 */
int sbull_revalidate(struct gendisk *gd)
{
	struct sbull_dev *dev = gd->private_data;
	
	if (dev->media_change) {
		dev->media_change = 0;
		printk(KERN_INFO "file_sbull: revalidate(not implemented).\n");
		// memset (dev->data, 0, dev->size);
	}
	return 0;
}

/*
 * The "invalidate" function runs out of the device timer; it sets
 * a flag to simulate the removal of the media.
 */
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 15, 0)) && !defined(timer_setup)
void sbull_invalidate(unsigned long ldev)
{
        struct sbull_dev *dev = (struct sbull_dev *) ldev;
#else
void sbull_invalidate(struct timer_list * ldev)
{
        struct sbull_dev *dev = from_timer(dev, ldev, timer);
#endif

	spin_lock(&dev->lock);
	if (dev->users)// || !dev->backing_file) //!dev->data) 
		printk (KERN_WARNING "sbull: timer sanity check failed\n");
	else
		dev->media_change = 1;
	spin_unlock(&dev->lock);
}

/*
 * The ioctl() implementation
 */

int sbull_ioctl (struct block_device *bdev, fmode_t mode,
                 unsigned int cmd, unsigned long arg)
{
	long size;
	struct hd_geometry geo;
	struct sbull_dev *dev = bdev->bd_disk->private_data;

	switch(cmd) {
	    case HDIO_GETGEO:
        	/*
		 * Get geometry: since we are a virtual device, we have to make
		 * up something plausible.  So we claim 16 sectors, four heads,
		 * and calculate the corresponding number of cylinders.  We set the
		 * start of data at sector four.
		 */
		size = dev->size*(hardsect_size/KERNEL_SECTOR_SIZE);
		geo.cylinders = (size & ~0x3f) >> 6;
		geo.heads = 4;
		geo.sectors = 16;
		geo.start = 4;
		if (copy_to_user((void __user *) arg, &geo, sizeof(geo)))
			return -EFAULT;
		return 0;
	}

	return -ENOTTY; /* unknown command */
}



/*
 * The device operations structure.
 */
static struct block_device_operations sbull_ops = {
	.owner           = THIS_MODULE,
	.open 	         = sbull_open,
	.release 	 = sbull_release,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 9, 0))
	.media_changed   = sbull_media_changed,  // DEPRECATED in v5.9
// #else
// 	.submit_bio      = sbull_make_request,
#endif
	.revalidate_disk = sbull_revalidate,
	.ioctl	         = sbull_ioctl
};


static struct blk_mq_ops mq_ops_full = {
    .queue_rq = sbull_full_request,
};

/*
 * Set up our internal device.
 */
static void setup_device(struct sbull_dev *dev, int which)
{
	/*
	 * Get some memory.
	 */

	printk(KERN_INFO "sbull: entered setup device");
	memset (dev, 0, sizeof (struct sbull_dev));
	dev->size = nsectors*hardsect_size;
	
	int d;
	for(d=0;d<NUM_DISKS;++d){
		char filename[200];
		sprintf(filename , "/home/sailendra/loopbackfile%d.img",d);
		dev->disk_devs[d].backing_file = filp_open(filename,O_RDWR , 0);
		dev->disk_devs[d].disk_thread = kthread_create(thread_test,&dev->disk_devs[d],filename);
		dev->disk_devs[d].num = d;
		my_bio_list_init(&dev->disk_devs[d].my_bio_list);
		wake_up_process(dev->disk_devs[d].disk_thread);
		init_waitqueue_head(&dev->disk_devs[d].req_event);
		spin_lock_init(&dev->disk_devs[d].lock);
	}
	struct task_struct* par_thread = kthread_create(parity_thread,dev,"parity_thread");
	wake_up_process(par_thread);
	init_waitqueue_head(&par_req_event);
	spin_lock_init(&par_lock);

	/*
	 * The timer which "invalidates" the device.
	 */
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 15, 0)) && !defined(timer_setup)
	init_timer(&dev->timer);
	dev->timer.data = (unsigned long) dev;
	dev->timer.function = sbull_invalidate;
#else
        timer_setup(&dev->timer, sbull_invalidate, 0);
#endif
		//dev->queue = blk_init_queue(sbull_full_request, &dev->lock);
		dev->queue = blk_mq_init_sq_queue(&dev->tag_set, &mq_ops_full, 128, BLK_MQ_F_SHOULD_MERGE);
		if (dev->queue == NULL)
			goto out_vfree;

	    

	blk_queue_logical_block_size(dev->queue, hardsect_size);
	dev->queue->queuedata = dev;
	/*
	 * And the gendisk structure.
	 */
	dev->gd = alloc_disk(SBULL_MINORS);
	if (! dev->gd) {
		printk (KERN_NOTICE "alloc_disk failure\n");
		goto out_vfree;
	}
	dev->gd->major = sbull_major;
	dev->gd->first_minor = which*SBULL_MINORS;
	dev->gd->fops = &sbull_ops;
	dev->gd->queue = dev->queue;
	dev->gd->private_data = dev;
	snprintf (dev->gd->disk_name, 32, "sbull%c", which + 'a');
	set_capacity(dev->gd, nsectors*(hardsect_size/KERNEL_SECTOR_SIZE));
	add_disk(dev->gd);
	return;

out_vfree:
	if (dev->data)
		vfree(dev->data);
	int i;	
	for(i=0;i<NUM_DISKS;++i){
		if (dev->disk_devs[i].backing_file)
			fput(dev->disk_devs[i].backing_file);
	}		
}



static int __init sbull_init(void)
{
	int i;
	/*
	 * Get registered.
	 */
	printk(KERN_INFO "file_sbull: sbull_init.\n");
	sbull_major = register_blkdev(sbull_major, "sbull");
	if (sbull_major <= 0) {
		printk(KERN_WARNING "sbull: unable to get major number\n");
		return -EBUSY;
	}
	/*
	 * Allocate the device array, and initialize each one.
	 */
	Devices = kmalloc(ndevices*sizeof (struct sbull_dev), GFP_KERNEL);
	if (Devices == NULL)
		goto out_unregister;
	for (i = 0; i < ndevices; i++) 
		setup_device(Devices + i, i);
    
	return 0;

	out_unregister:
		unregister_blkdev(sbull_major, "sbd");
		return -ENOMEM;
}

static void sbull_exit(void)
{
	int i;
	printk(KERN_INFO "file_sbull: sbull_exit.\n");
	for (i = 0; i < ndevices; i++) {
		struct sbull_dev *dev = Devices + i;
		del_timer_sync(&dev->timer);
		if (dev->gd) {
			del_gendisk(dev->gd);
			put_disk(dev->gd);
		}
		if (dev->queue) {
			blk_cleanup_queue(dev->queue);
		}
		if (dev->data)
			vfree(dev->data);
		int j;	
		for(j=0;j<NUM_DISKS;++j){
			if (dev->disk_devs[j].backing_file)
				fput(dev->disk_devs[j].backing_file);
		}	
	}
	unregister_blkdev(sbull_major, "sbull");
	kfree(Devices);
}
	
module_init(sbull_init);
module_exit(sbull_exit);
