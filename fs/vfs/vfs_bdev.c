
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <osv/device.h>
#include <osv/prex.h>
#include <osv/buf.h>
#include <osv/bio.h>

int
bdev_read(struct device *dev, struct uio *uio, int ioflags)
{
	struct buf *bp;
	int ret;

	assert(uio->uio_rw == UIO_READ);

	/*
	 * These should be handled gracefully, but this gives us the
	 * best debugging for now.
	 */
	assert((uio->uio_offset % BSIZE) == 0);
	assert((uio->uio_resid % BSIZE) == 0);

	if (uio->uio_offset < 0)
		return EINVAL;
	if (uio->uio_resid == 0)
		return 0;
    
	while (uio->uio_resid > 0) {
		struct iovec *iov = uio->uio_iov;

		if (!iov->iov_len)
			continue;

		ret = bread(dev, uio->uio_offset >> 9, &bp);
		if (ret)
			return ret;

		ret = uiomove(bp->b_data, BSIZE, uio);
		brelse(bp);

		if (ret)
			return ret;
	}

	return 0;
}

int
bdev_write(struct device *dev, struct uio *uio, int ioflags)
{
	struct buf *bp;
	int ret;

	assert(uio->uio_rw == UIO_WRITE);

	/*
	 * These should be handled gracefully, but this gives us the
	 * best debugging for now.
	 */
	assert((uio->uio_offset % BSIZE) == 0);
	assert((uio->uio_resid % BSIZE) == 0);

	if (uio->uio_offset < 0)
		return EINVAL;
	if (uio->uio_resid == 0)
		return 0;
    
	while (uio->uio_resid > 0) {
		struct iovec *iov = uio->uio_iov;

		if (!iov->iov_len)
			continue;

		bp = getblk(dev, uio->uio_offset >> 9);

		ret = uiomove(bp->b_data, BSIZE, uio);
		if (ret) {
			brelse(bp);
			return ret;
		}

		ret = bwrite(bp);
		if (ret)
			return ret;
	}

	return 0;
}


int
physio(struct device *dev, struct uio *uio, int ioflags)
{
	struct bio *bio;

	if (uio->uio_offset < 0)
		return EINVAL;
	if (uio->uio_resid == 0)
		return 0;
    
	while (uio->uio_resid > 0) {
		struct iovec *iov = uio->uio_iov;

		if (!iov->iov_len)
			continue;

		bio = alloc_bio();
		if (!bio)
			return ENOMEM;

		if (uio->uio_rw == UIO_READ)
			bio->bio_cmd = BIO_READ;
		else
			bio->bio_cmd = BIO_WRITE;

		bio->bio_dev = dev;
		bio->bio_data = iov->iov_base;
		bio->bio_offset = uio->uio_offset;
		bio->bio_bcount = uio->uio_resid;

		dev->driver->devops->strategy(bio);

		pthread_mutex_lock(&bio->bio_mutex);
		while (!(bio->bio_flags & BIO_DONE))
			pthread_cond_wait(&bio->bio_wait, &bio->bio_mutex);
		pthread_mutex_unlock(&bio->bio_mutex);

		destroy_bio(bio);

	        uio->uio_iov++;
        	uio->uio_iovcnt--;
        	uio->uio_resid -= iov->iov_len;
        	uio->uio_offset += iov->iov_len;
	}

	return 0;
}
