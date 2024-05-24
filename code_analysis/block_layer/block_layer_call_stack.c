// 块层调用栈

//*******************************************************
//*******************************************************
/**
 * submit_bio - submit a bio to the block device layer for I/O
 * @bio: The &struct bio which describes the I/O
 *
 * submit_bio() is used to submit I/O requests to block devices.  It is passed a
 * fully set up &struct bio that describes the I/O that needs to be done.  The
 * bio will be send to the device described by the bi_bdev field.
 *
 * The success/failure status of the request, along with notification of
 * completion, is delivered asynchronously through the ->bi_end_io() callback
 * in @bio.  The bio must NOT be touched by the caller until ->bi_end_io() has
 * been called.
 */
void submit_bio(struct bio *bio)
{
    // 检查bio结构中的操作类型是否为读取操作
	if (bio_op(bio) == REQ_OP_READ) {
        // 如果是读取操作，则记录读取操作的字节数
		task_io_account_read(bio->bi_iter.bi_size);
        // 统计页面输入事件，并传递bio中扇区的数量作为参数
		count_vm_events(PGPGIN, bio_sectors(bio));
	} 
    // 如果操作类型为写入操作
    else if (bio_op(bio) == REQ_OP_WRITE) {
        // 统计页面输出事件，并传递bio中扇区的数量作为参数
		count_vm_events(PGPGOUT, bio_sectors(bio));
	}

    // 设置bio结构中的I/O优先级
	bio_set_ioprio(bio);
    // 将构建好的bio结构提交给内核进行I/O操作，但不计入I/O统计信息
	submit_bio_noacct(bio);
}





/**
 * submit_bio_noacct - re-submit a bio to the block device layer for I/O
 * @bio:  The bio describing the location in memory and on the device.
 *
 * This is a version of submit_bio() that shall only be used for I/O that is
 * resubmitted to lower level drivers by stacking block drivers.  All file
 * systems and other upper level users of the block layer should use
 * submit_bio() instead.
 */
void submit_bio_noacct(struct bio *bio)
{
    // 获取bio所属的块设备
	struct block_device *bdev = bio->bi_bdev;
    // 获取块设备对应的请求队列
	struct request_queue *q = bdev_get_queue(bdev);
    // 初始化块设备状态为I/O错误
	blk_status_t status = BLK_STS_IOERR;

    // 休眠前先检查是否可能休眠
	might_sleep();

    // 对于基于REQ_NOWAIT的请求，如果队列不支持NOWAIT，则返回-EOPNOTSUPP
	if ((bio->bi_opf & REQ_NOWAIT) && !bdev_nowait(bdev))
		goto not_supported;

    // 如果应该失败bio，则直接结束I/O处理
	if (should_fail_bio(bio))
		goto end_io;
    // 检查bio是否为只读
	bio_check_ro(bio);
    // 如果bio没有被重映射
	if (!bio_flagged(bio, BIO_REMAPPED)) {
        // 如果遇到了边界条件，则直接结束I/O处理
		if (unlikely(bio_check_eod(bio)))
			goto end_io;
        // 如果有分区号且在重映射时出现了异常，则直接结束I/O处理
		if (bdev->bd_partno && unlikely(blk_partition_remap(bio)))
			goto end_io;
	}

    // 提前过滤刷新的bio，以便不支持刷新的基于bio的驱动程序不必担心它们
	if (op_is_flush(bio->bi_opf)) {
        // 如果是刷新操作但不支持写入或区域附加操作，则直接结束I/O处理
		if (WARN_ON_ONCE(bio_op(bio) != REQ_OP_WRITE &&
				 bio_op(bio) != REQ_OP_ZONE_APPEND))
			goto end_io;
        // 如果队列不支持写回缓存，则清除相关标志位并检查是否有扇区，如果没有则直接结束I/O处理
		if (!test_bit(QUEUE_FLAG_WC, &q->queue_flags)) {
			bio->bi_opf &= ~(REQ_PREFLUSH | REQ_FUA);
			if (!bio_sectors(bio)) {
				status = BLK_STS_OK;
				goto end_io;
			}
		}
	}

    // 如果队列不是轮询队列，则清除轮询标志位
	if (!test_bit(QUEUE_FLAG_POLL, &q->queue_flags))
		bio_clear_polled(bio);

    // 根据不同的操作类型进行处理
	switch (bio_op(bio)) {
    // 读取和写入操作直接返回
	case REQ_OP_READ:
	case REQ_OP_WRITE:
		break;
    // 刷新操作不能通过bio提交，而是在请求结构中合成
	case REQ_OP_FLUSH:
		goto not_supported;
    // 丢弃操作如果设备不支持则直接返回
	case REQ_OP_DISCARD:
		if (!bdev_max_discard_sectors(bdev))
			goto not_supported;
		break;
    // 安全擦除操作如果设备不支持则直接返回
	case REQ_OP_SECURE_ERASE:
		if (!bdev_max_secure_erase_sectors(bdev))
			goto not_supported;
		break;
    // 区域附加操作需要检查是否可用
	case REQ_OP_ZONE_APPEND:
        // 检查区域附加操作的可用性
		status = blk_check_zone_append(q, bio);
        // 如果不可用，则直接结束I/O处理
		if (status != BLK_STS_OK)
			goto end_io;
		break;
    // 写入零值操作如果不支持则直接返回
	case REQ_OP_WRITE_ZEROES:
		if (!q->limits.max_write_zeroes_sectors)
			goto not_supported;
		break;
    // 区域重置、区域打开、区域关闭和区域完成操作需要检查设备是否支持区域
	case REQ_OP_ZONE_RESET:
	case REQ_OP_ZONE_OPEN:
	case REQ_OP_ZONE_CLOSE:
	case REQ_OP_ZONE_FINISH:
        // 如果设备不是分区的则直接返回
		if (!bdev_is_zoned(bio->bi_bdev))
			goto not_supported;
		break;
    // 区域重置所有操作需要检查设备是否支持区域重置所有操作
	case REQ_OP_ZONE_RESET_ALL:
        // 如果设备不是分区的或队列不支持区域重置所有操作则直接返回
		if (!bdev_is_zoned(bio->bi_bdev) || !blk_queue_zone_resetall(q))
			goto not_supported;
		break;
    // 驱动程序私有操作只用于直接请求
	case REQ_OP_DRV_IN:
	case REQ_OP_DRV_OUT:
        // 没有处理，直接穿透到默认情况
		fallthrough;
    // 默认情况下，不支持的操作类型直接返回
	default:
		goto not_supported;
	}

    // 如果bio受到了块限流器的控制，则直接返回
	if (blk_throtl_bio(bio))
		return;
    // 提交bio，但不统计I/O，不检查
	submit_bio_noacct_nocheck(bio);
	return;

not_supported:
    // 设置块设备状态为不支持
	status = BLK_STS_NOTSUPP;
end_io:
    // 设置bio的状态，并结束I/O处理
	bio->bi_status = status;
	bio_endio(bio);
}

static void bio_set_ioprio(struct bio *bio)
{
    /* 如果之前没有设置I/O优先级？根据任务的nice值初始化它 */
	if (IOPRIO_PRIO_CLASS(bio->bi_ioprio) == IOPRIO_CLASS_NONE)
		bio->bi_ioprio = get_current_ioprio();
    // 设置块控制组的I/O优先级
	blkcg_set_ioprio(bio);
}


EXPORT_SYMBOL(submit_bio_noacct);

static void bio_set_ioprio(struct bio *bio)
{
	/* Nobody set ioprio so far? Initialize it based on task's nice value */
	if (IOPRIO_PRIO_CLASS(bio->bi_ioprio) == IOPRIO_CLASS_NONE)
		bio->bi_ioprio = get_current_ioprio();
	blkcg_set_ioprio(bio);
}

void submit_bio_noacct_nocheck(struct bio *bio)
{
	blk_cgroup_bio_start(bio);
	blkcg_bio_issue_init(bio);

	if (!bio_flagged(bio, BIO_TRACE_COMPLETION)) {
		trace_block_bio_queue(bio);
		/*
		 * Now that enqueuing has been traced, we need to trace
		 * completion as well.
		 */
		bio_set_flag(bio, BIO_TRACE_COMPLETION);
	}

	/*
	 * We only want one ->submit_bio to be active at a time, else stack
	 * usage with stacked devices could be a problem.  Use current->bio_list
	 * to collect a list of requests submited by a ->submit_bio method while
	 * it is active, and then process them after it returned.
	 */
	if (current->bio_list)
		bio_list_add(&current->bio_list[0], bio);
	else if (!bio->bi_bdev->bd_has_submit_bio)
		__submit_bio_noacct_mq(bio);
	else
		__submit_bio_noacct(bio);
}

/*
 *这段代码是一个用于提交块输入/输出请求（block I/O）的函数 __submit_bio_noacct。让我解释一下它的主要功能：
 *
 *首先，它声明了一个名为 bio_list_on_stack 的数组，数组中有两个 struct bio_list 结构。
 *通过 BUG_ON(bio->bi_next); 来确保输入的 bio 是一个单独的请求，而不是一个连接的链表结构。
 *bio_list_init(&bio_list_on_stack[0]); 初始化了 bio_list_on_stack 数组的第一个元素。
 *将当前任务的 bio_list 字段设置为 bio_list_on_stack，这样可以在整个函数中使用该数组来跟踪请求。
 *进入一个 do-while 循环，直到栈中没有更多的 bios。
 *在循环内部：
 *a. 获取当前 bio 所属的请求队列。
 *b. 初始化两个 struct bio_list 结构 lower 和 same，用于根据请求队列将 bios 分为较低级和相同级别的请求。
 *c. 遍历栈中的 bios，并根据其所属的请求队列将每个 bio 移动到 lower 或 same 列表中。
 *d. 合并 lower、same 和之前的栈内容，以确保首先处理较低级别设备的 bios。
 *在处理完所有 bios 后，将 current->bio_list 设置为 NULL，表示没有挂起的 bios。
 *综上所述，这段代码主要用于处理和提交块输入/输出请求，并确保按照特定的顺序进行处理以提高性能或处理不同块设备之间的依赖关系。
 */


/*
 * The loop in this function may be a bit non-obvious, and so deserves some
 * explanation:
 *
 *  - Before entering the loop, bio->bi_next is NULL (as all callers ensure
 *    that), so we have a list with a single bio.
 *  - We pretend that we have just taken it off a longer list, so we assign
 *    bio_list to a pointer to the bio_list_on_stack, thus initialising the
 *    bio_list of new bios to be added.  ->submit_bio() may indeed add some more
 *    bios through a recursive call to submit_bio_noacct.  If it did, we find a
 *    non-NULL value in bio_list and re-enter the loop from the top.
 *  - In this case we really did just take the bio of the top of the list (no
 *    pretending) and so remove it from bio_list, and call into ->submit_bio()
 *    again.
 *
 * bio_list_on_stack[0] contains bios submitted by the current ->submit_bio.
 * bio_list_on_stack[1] contains bios that were submitted before the current
 *	->submit_bio, but that haven't been processed yet.
 */

static void __submit_bio_noacct(struct bio *bio)
{
	struct bio_list bio_list_on_stack[2];

	BUG_ON(bio->bi_next);

	bio_list_init(&bio_list_on_stack[0]);
	current->bio_list = bio_list_on_stack;

	do {
		struct request_queue *q = bdev_get_queue(bio->bi_bdev);
		struct bio_list lower, same;

		/*
		 * Create a fresh bio_list for all subordinate requests.
		 */
		bio_list_on_stack[1] = bio_list_on_stack[0];
		bio_list_init(&bio_list_on_stack[0]);

		__submit_bio(bio);

		/*
		 * Sort new bios into those for a lower level and those for the
		 * same level.
		 */
		bio_list_init(&lower);
		bio_list_init(&same);
		while ((bio = bio_list_pop(&bio_list_on_stack[0])) != NULL)
			if (q == bdev_get_queue(bio->bi_bdev))
				bio_list_add(&same, bio);
			else
				bio_list_add(&lower, bio);

		/*
		 * Now assemble so we handle the lowest level first.
		 */
		bio_list_merge(&bio_list_on_stack[0], &lower);
		bio_list_merge(&bio_list_on_stack[0], &same);
		bio_list_merge(&bio_list_on_stack[0], &bio_list_on_stack[1]);
	} while ((bio = bio_list_pop(&bio_list_on_stack[0])));

	current->bio_list = NULL;
}

static void __submit_bio_noacct_mq(struct bio *bio)
{
	struct bio_list bio_list[2] = { };

	current->bio_list = bio_list;

	do {
		__submit_bio(bio);
	} while ((bio = bio_list_pop(&bio_list[0])));

	current->bio_list = NULL;
}


static void __submit_bio(struct bio *bio)
{
	if (unlikely(!blk_crypto_bio_prep(&bio)))
		return;

	if (!bio->bi_bdev->bd_has_submit_bio) {
		blk_mq_submit_bio(bio);
	} else if (likely(bio_queue_enter(bio) == 0)) {
		struct gendisk *disk = bio->bi_bdev->bd_disk;

		disk->fops->submit_bio(bio);
		blk_queue_exit(disk->queue);
	}
}
