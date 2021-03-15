#include <linux/version.h> 	/* LINUX_VERSION_CODE  */
#include <linux/kernel.h>	/* printk() */
#include <linux/slab.h>		/* kmalloc() */
#include <linux/fs.h>		/* everything... */
#include <linux/errno.h>	/* error codes */
#include <linux/types.h>	/* size_t */
#include <linux/bio.h>
#include <linux/semaphore.h>

struct my_bio{
	struct my_bio* bi_next;
	struct bio* bio;
	int disk_num;
	int parity_disk;
};

struct my_bio_list {
	struct my_bio* head;
	struct my_bio* tail;
};

struct disk_dev{
	struct task_struct* disk_thread;
	struct bio_list bio_list;
	spinlock_t lock;
	struct file* backing_file;
	wait_queue_head_t req_event;
	struct my_bio_list my_bio_list;
	int num;
};


struct par_dev{
	struct par_dev* bi_next;
	struct bio* par_bio;
	int disk_num;
	int parity_disk;
	struct semaphore par_lock;
	bool flag;
};

struct par_list {
	struct par_dev* head;
	struct par_dev* tail;
};

static inline int my_bio_list_empty(const struct my_bio_list *bl)
{
	return bl->head == NULL;
}

static inline void my_bio_list_init(struct my_bio_list *bl)
{
	bl->head = bl->tail = NULL;
}

static inline void my_bio_list_add(struct my_bio_list *bl, struct my_bio *bio)
{
	bio->bi_next = NULL;

	if (bl->tail)
		bl->tail->bi_next = bio;
	else
		bl->head = bio;

	bl->tail = bio;
}

static inline struct my_bio *my_bio_list_pop(struct my_bio_list *bl)
{
	struct my_bio *bio = bl->head;

	if (bio) {
		bl->head = bl->head->bi_next;
		if (!bl->head)
			bl->tail = NULL;

		bio->bi_next = NULL;
	}

	return bio;
}


static inline int par_list_empty(const struct par_list *bl)
{
	return bl->head == NULL;
}

static inline void par_list_init(struct par_list *bl)
{
	bl->head = bl->tail = NULL;
}

static inline void par_list_add(struct par_list *bl, struct par_dev *bio)
{
	bio->bi_next = NULL;

	if (bl->tail)
		bl->tail->bi_next = bio;
	else
		bl->head = bio;

	bl->tail = bio;
}

static inline struct par_dev *par_list_pop(struct par_list *bl)
{
	struct par_dev *bio = bl->head;

	if (bio) {
		bl->head = bl->head->bi_next;
		if (!bl->head)
			bl->tail = NULL;

		bio->bi_next = NULL;
	}

	return bio;
}
