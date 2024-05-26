// multi queue调用栈

//***************************** request_queue初始化 *******************************

/*
初始化一个块设备请求队列（request_queue），并将其与给定的多队列标签集（blk_mq_tag_set）相关联。
*/
struct request_queue *blk_mq_init_queue(struct blk_mq_tag_set *set)
{
	struct request_queue *uninit_q, *q;

	uninit_q = blk_alloc_queue_node(GFP_KERNEL, set->numa_node);
	if (!uninit_q)
		return ERR_PTR(-ENOMEM);
	/*
	调用 blk_alloc_queue_node 函数分配一个未初始化的请求队列 uninit_q，
	并将其与特定的NUMA节点关联。如果分配失败，则返回内存分配错误（ENOMEM）的错误指针
	*/
	/*
	 * Initialize the queue without an elevator. device_add_disk() will do
	 * the initialization.
	 */
	q = blk_mq_init_allocated_queue(set, uninit_q, false);
	if (IS_ERR(q))
		blk_cleanup_queue(uninit_q);

	return q;
}
EXPORT_SYMBOL(blk_mq_init_queue);

struct request_queue *blk_mq_init_allocated_queue(struct blk_mq_tag_set *set,
						  struct request_queue *q,
						  bool elevator_init)
{
	/* mark the queue as mq asap */
	q->mq_ops = set->ops;
	// 标记q为多队列
	q->poll_cb = blk_stat_alloc_callback(blk_mq_poll_stats_fn,
					     blk_mq_poll_stats_bkt,
					     BLK_MQ_POLL_STATS_BKTS, q);

	if (!q->poll_cb)
		goto err_exit;
	// 设置轮询回调函数

	if (blk_mq_alloc_ctxs(q))
		goto err_poll;

	/* init q->mq_kobj and sw queues' kobjects */
	blk_mq_sysfs_init(q);

	q->nr_queues = nr_hw_queues(set);
	q->queue_hw_ctx = kcalloc_node(q->nr_queues, sizeof(*(q->queue_hw_ctx)),
						GFP_KERNEL, set->numa_node);
	if (!q->queue_hw_ctx)
		goto err_sys_init;

	INIT_LIST_HEAD(&q->unused_hctx_list);
	spin_lock_init(&q->unused_hctx_lock);

	//创建硬件队列
	blk_mq_realloc_hw_ctxs(set, q);
	
	if (!q->nr_hw_queues)
		goto err_hctxs;

	INIT_WORK(&q->timeout_work, blk_mq_timeout_work);
	blk_queue_rq_timeout(q, set->timeout ? set->timeout : 30 * HZ);

	q->tag_set = set;

	q->queue_flags |= QUEUE_FLAG_MQ_DEFAULT;
	if (set->nr_maps > HCTX_TYPE_POLL &&
	    set->map[HCTX_TYPE_POLL].nr_queues)
		blk_queue_flag_set(QUEUE_FLAG_POLL, q);

	q->sg_reserved_size = INT_MAX;

	INIT_DELAYED_WORK(&q->requeue_work, blk_mq_requeue_work);
	INIT_LIST_HEAD(&q->requeue_list);
	spin_lock_init(&q->requeue_lock);

	// 设置io请求入口函数为blk-mq通用接口
	blk_queue_make_request(q, blk_mq_make_request);

	/*
	 * Do this after blk_queue_make_request() overrides it...
	 */
	q->nr_requests = set->queue_depth;

	/*
	 * Default to classic polling
	 */
	q->poll_nsec = BLK_MQ_POLL_CLASSIC;

	//创建cpu个数个软件队列
	blk_mq_init_cpu_queues(q, set->nr_hw_queues);
	blk_mq_add_queue_tag_set(set, q);

	// 软硬件队列绑定
	blk_mq_map_swqueue(q);

	if (elevator_init)
		elevator_init_mq(q);

	return q;

err_hctxs:
	kfree(q->queue_hw_ctx);
	q->nr_hw_queues = 0;
err_sys_init:
	blk_mq_sysfs_deinit(q);
err_poll:
	blk_stat_free_callback(q->poll_cb);
	q->poll_cb = NULL;
err_exit:
	q->mq_ops = NULL;
	return ERR_PTR(-ENOMEM);
}
EXPORT_SYMBOL(blk_mq_init_allocated_queue);

// subfunctions   次级函数
{

	static void blk_mq_realloc_hw_ctxs(struct blk_mq_tag_set *set,
							struct request_queue *q)
	{
		int i, j, end;
		struct blk_mq_hw_ctx **hctxs = q->queue_hw_ctx;

		/* protect against switching io scheduler  */
		mutex_lock(&q->sysfs_lock);
		for (i = 0; i < set->nr_hw_queues; i++) {
			int node;
			struct blk_mq_hw_ctx *hctx;

			node = blk_mq_hw_queue_to_node(&set->map[HCTX_TYPE_DEFAULT], i);
			/*
			* If the hw queue has been mapped to another numa node,
			* we need to realloc the hctx. If allocation fails, fallback
			* to use the previous one.
			*/
			if (hctxs[i] && (hctxs[i]->numa_node == node))
				continue;

			hctx = blk_mq_alloc_and_init_hctx(set, q, i, node);
			if (hctx) {
				if (hctxs[i])
					blk_mq_exit_hctx(q, set, hctxs[i], i);
				hctxs[i] = hctx;
			} else {
				if (hctxs[i])
					pr_warn("Allocate new hctx on node %d fails,\
							fallback to previous one on node %d\n",
							node, hctxs[i]->numa_node);
				else
					break;
			}
		}
		/*
		* Increasing nr_hw_queues fails. Free the newly allocated
		* hctxs and keep the previous q->nr_hw_queues.
		*/
		if (i != set->nr_hw_queues) {
			j = q->nr_hw_queues;
			end = i;
		} else {
			j = i;
			end = q->nr_hw_queues;
			q->nr_hw_queues = set->nr_hw_queues;
		}

		for (; j < end; j++) {
			struct blk_mq_hw_ctx *hctx = hctxs[j];

			if (hctx) {
				if (hctx->tags)
					blk_mq_free_map_and_requests(set, j);
				blk_mq_exit_hctx(q, set, hctx, j);
				hctxs[j] = NULL;
			}
		}
		mutex_unlock(&q->sysfs_lock);
	}

	/**
	* blk_queue_make_request - define an alternate make_request function for a device
	* @q:  the request queue for the device to be affected
	* @mfn: the alternate make_request function
	*
	* Description:
	*    The normal way for &struct bios to be passed to a device
	*    driver is for them to be collected into requests on a request
	*    queue, and then to allow the device driver to select requests
	*    off that queue when it is ready.  This works well for many block
	*    devices. However some block devices (typically virtual devices
	*    such as md or lvm) do not benefit from the processing on the
	*    request queue, and are served best by having the requests passed
	*    directly to them.  This can be achieved by providing a function
	*    to blk_queue_make_request().
	*
	* Caveat:
	*    The driver that does this *must* be able to deal appropriately
	*    with buffers in "highmemory". This can be accomplished by either calling
	*    kmap_atomic() to get a temporary kernel mapping, or by calling
	*    blk_queue_bounce() to create a buffer in normal memory.
	**/
	void blk_queue_make_request(struct request_queue *q, make_request_fn *mfn)
	{
		/*
		* set defaults
		*/
		q->nr_requests = BLKDEV_MAX_RQ;

		q->make_request_fn = mfn;
		blk_queue_dma_alignment(q, 511);

		blk_set_default_limits(&q->limits);
	}

	static void blk_mq_init_cpu_queues(struct request_queue *q,
					unsigned int nr_hw_queues)
	{
		struct blk_mq_tag_set *set = q->tag_set;
		unsigned int i, j;

		for_each_possible_cpu(i) {
			struct blk_mq_ctx *__ctx = per_cpu_ptr(q->queue_ctx, i);
			struct blk_mq_hw_ctx *hctx;
			int k;

			__ctx->cpu = i;
			spin_lock_init(&__ctx->lock);
			for (k = HCTX_TYPE_DEFAULT; k < HCTX_MAX_TYPES; k++)
				INIT_LIST_HEAD(&__ctx->rq_lists[k]);

			__ctx->queue = q;

			/*
			* Set local node, IFF we have more than one hw queue. If
			* not, we remain on the home node of the device
			*/
			for (j = 0; j < set->nr_maps; j++) {
				hctx = blk_mq_map_queue_type(q, j, i);
				if (nr_hw_queues > 1 && hctx->numa_node == NUMA_NO_NODE)
					hctx->numa_node = local_memory_node(cpu_to_node(i));
			}
		}
	}

	static void blk_mq_map_swqueue(struct request_queue *q)
	{
		unsigned int i, j, hctx_idx;
		struct blk_mq_hw_ctx *hctx;
		struct blk_mq_ctx *ctx;
		struct blk_mq_tag_set *set = q->tag_set;

		queue_for_each_hw_ctx(q, hctx, i) {
			cpumask_clear(hctx->cpumask);
			hctx->nr_ctx = 0;
			hctx->dispatch_from = NULL;
		}

		/*
		* Map software to hardware queues.
		*
		* If the cpu isn't present, the cpu is mapped to first hctx.
		*/
		for_each_possible_cpu(i) {

			ctx = per_cpu_ptr(q->queue_ctx, i);
			for (j = 0; j < set->nr_maps; j++) {
				if (!set->map[j].nr_queues) {
					ctx->hctxs[j] = blk_mq_map_queue_type(q,
							HCTX_TYPE_DEFAULT, i);
					continue;
				}
				hctx_idx = set->map[j].mq_map[i];
				/* unmapped hw queue can be remapped after CPU topo changed */
				if (!set->tags[hctx_idx] &&
					!__blk_mq_alloc_rq_map(set, hctx_idx)) {
					/*
					* If tags initialization fail for some hctx,
					* that hctx won't be brought online.  In this
					* case, remap the current ctx to hctx[0] which
					* is guaranteed to always have tags allocated
					*/
					set->map[j].mq_map[i] = 0;
				}

				hctx = blk_mq_map_queue_type(q, j, i);
				ctx->hctxs[j] = hctx;
				/*
				* If the CPU is already set in the mask, then we've
				* mapped this one already. This can happen if
				* devices share queues across queue maps.
				*/
				if (cpumask_test_cpu(i, hctx->cpumask))
					continue;

				cpumask_set_cpu(i, hctx->cpumask);
				hctx->type = j;
				ctx->index_hw[hctx->type] = hctx->nr_ctx;
				hctx->ctxs[hctx->nr_ctx++] = ctx;

				/*
				* If the nr_ctx type overflows, we have exceeded the
				* amount of sw queues we can support.
				*/
				BUG_ON(!hctx->nr_ctx);
			}

			for (; j < HCTX_MAX_TYPES; j++)
				ctx->hctxs[j] = blk_mq_map_queue_type(q,
						HCTX_TYPE_DEFAULT, i);
		}

		queue_for_each_hw_ctx(q, hctx, i) {
			/*
			* If no software queues are mapped to this hardware queue,
			* disable it and free the request entries.
			*/
			if (!hctx->nr_ctx) {
				/* Never unmap queue 0.  We need it as a
				* fallback in case of a new remap fails
				* allocation
				*/
				if (i && set->tags[i])
					blk_mq_free_map_and_requests(set, i);

				hctx->tags = NULL;
				continue;
			}

			hctx->tags = set->tags[i];
			WARN_ON(!hctx->tags);

			/*
			* Set the map size to the number of mapped software queues.
			* This is more accurate and more efficient than looping
			* over all possibly mapped software queues.
			*/
			sbitmap_resize(&hctx->ctx_map, hctx->nr_ctx);

			/*
			* Initialize batch roundrobin counts
			*/
			hctx->next_cpu = blk_mq_first_mapped_cpu(hctx);
			hctx->next_cpu_batch = BLK_MQ_CPU_WORK_BATCH;
		}
	}

}

//***************************** IO提交/转换request *******************************
// 处理由上层传递下来的 bio 结构体，将其转换为 request 并提交到块设备的请求队列中。
static blk_qc_t blk_mq_make_request(struct request_queue *q, struct bio *bio)
{
	const int is_sync = op_is_sync(bio->bi_opf);
	const int is_flush_fua = op_is_flush(bio->bi_opf);
	struct blk_mq_alloc_data data = { .flags = 0};
	struct request *rq;
	struct blk_plug *plug;
	struct request *same_queue_rq = NULL;
	unsigned int nr_segs;
	blk_qc_t cookie;

	blk_queue_bounce(q, &bio);
	__blk_queue_split(q, &bio, &nr_segs);

	if (!bio_integrity_prep(bio))
		return BLK_QC_T_NONE;

	if (!is_flush_fua && !blk_queue_nomerges(q) &&
	    blk_attempt_plug_merge(q, bio, nr_segs, &same_queue_rq))
		// 尝试将bio合并至plug队列里的rq，若合并成功直接返回
		return BLK_QC_T_NONE;

	if (blk_mq_sched_bio_merge(q, bio, nr_segs))
	   // 尝试将bio合并至调度器队列里的rq，若合并成功直接返回
		return BLK_QC_T_NONE;

	rq_qos_throttle(q, bio);
	// 执行限流策略

	data.cmd_flags = bio->bi_opf;

	rq = blk_mq_get_request(q, bio, &data);
	// 从硬件队列的tags里获取rq
	// 从请求队列的硬件队列中获取一个空闲的请求结构体

	if (unlikely(!rq)) {
		rq_qos_cleanup(q, bio);
		if (bio->bi_opf & REQ_NOWAIT)
			bio_wouldblock_error(bio);
		return BLK_QC_T_NONE;
	}

	trace_block_getrq(q, bio, bio->bi_opf);

	rq_qos_track(q, rq, bio);

	cookie = request_to_qc_t(data.hctx, rq);

	blk_mq_bio_to_request(rq, bio, nr_segs);
	// 根据bio参数设置rq参数  转换bio为request



	plug = blk_mq_plug(q, bio);
	if (unlikely(is_flush_fua)) {
		/* bypass scheduler for flush rq */

		// 如果是flush/fua请求则插入福禄寿队列后启动派发
		blk_insert_flush(rq);
		blk_mq_run_hw_queue(data.hctx, true);

	} else if (plug && (q->nr_hw_queues == 1 || q->mq_ops->commit_rqs ||
				!blk_queue_nonrot(q))) {
		/*
		 * Use plugging if we have a ->commit_rqs() hook as well, as
		 * we know the driver uses bd->last in a smart fashion.
		 *
		 * Use normal plugging if this disk is slow HDD, as sequential
		 * IO may benefit a lot from plug merging.
		 */
		unsigned int request_count = plug->rq_count;
		struct request *last = NULL;

		if (!request_count)
			trace_block_plug(q);
		else
			last = list_entry_rq(plug->mq_list.prev);

		if (request_count >= BLK_MAX_REQUEST_COUNT || (last &&
		    blk_rq_bytes(last) >= BLK_PLUG_FLUSH_SIZE)) {
			// plug队列请求数量超过上限

			blk_flush_plug_list(plug, false);
			// 将plug队列的rq加入调度队列
			trace_block_plug(q);
		}

		blk_add_rq_to_plug(plug, rq);
		// rq加入plug队列

	} else if (q->elevator) {
		// 使用调度器
		// rq插入调度器队列并启动派发
		blk_mq_sched_insert_request(rq, false, true, true);
	} else if (plug && !blk_queue_nomerges(q)) {
		/*
		 * We do limited plugging. If the bio can be merged, do that.
		 * Otherwise the existing request in the plug list will be
		 * issued. So the plug list will have one request at most
		 * The plug list might get flushed before this. If that happens,
		 * the plug list is empty, and same_queue_rq is invalid.
		 */
		if (list_empty(&plug->mq_list))
			same_queue_rq = NULL;
		if (same_queue_rq) {
			list_del_init(&same_queue_rq->queuelist);
			plug->rq_count--;
		}
		blk_add_rq_to_plug(plug, rq);
		trace_block_plug(q);

		if (same_queue_rq) {
			data.hctx = same_queue_rq->mq_hctx;
			trace_block_unplug(q, 1, true);
			blk_mq_try_issue_directly(data.hctx, same_queue_rq,
					&cookie);
		}
	} else if ((q->nr_hw_queues > 1 && is_sync) ||
			!data.hctx->dispatch_busy) {
		blk_mq_try_issue_directly(data.hctx, rq, &cookie);
		// 直接派发
	} else {
		blk_mq_sched_insert_request(rq, false, true, true);
		// rq加入调度器队列，并启动派发
	}

	return cookie;
}

//***************************** request获取 *******************************

static struct request *blk_mq_get_request(struct request_queue *q,
					  struct bio *bio,
					  struct blk_mq_alloc_data *data)
{
	struct elevator_queue *e = q->elevator;
	struct request *rq;
	unsigned int tag;
	bool clear_ctx_on_error = false;
	u64 alloc_time_ns = 0;

	blk_queue_enter_live(q);

	/* alloc_time includes depth and tag waits */
	if (blk_queue_rq_alloc_time(q))
		alloc_time_ns = ktime_get_ns();

	data->q = q;
	if (likely(!data->ctx)) {
		data->ctx = blk_mq_get_ctx(q);
		// 找到软件队列关联的硬件队列
		clear_ctx_on_error = true;
		
	}
	if (likely(!data->hctx))
		data->hctx = blk_mq_map_queue(q, data->cmd_flags,
						data->ctx);
		// 找到软件队列关联的硬件队列
	if (data->cmd_flags & REQ_NOWAIT)
		data->flags |= BLK_MQ_REQ_NOWAIT;

	if (e) {
		data->flags |= BLK_MQ_REQ_INTERNAL;

		/*
		 * Flush requests are special and go directly to the
		 * dispatch list. Don't include reserved tags in the
		 * limiting, as it isn't useful.
		 */
		if (!op_is_flush(data->cmd_flags) &&
		    e->type->ops.limit_depth &&
		    !(data->flags & BLK_MQ_REQ_RESERVED))
			e->type->ops.limit_depth(data->cmd_flags, data);
			// 若有调度器，更新队列深度限制
	} else {
		blk_mq_tag_busy(data->hctx);
		// 无调度器，tag_set是共享的，增加其引用。
	}

	tag = blk_mq_get_tag(data);
	if (tag == BLK_MQ_TAG_FAIL) {
		if (clear_ctx_on_error)
			data->ctx = NULL;
		blk_queue_exit(q);
		return NULL;
	}

	rq = blk_mq_rq_ctx_init(data, tag, data->cmd_flags, alloc_time_ns);
	// 获取rq并且初始化
	if (!op_is_flush(data->cmd_flags)) {
		rq->elv.icq = NULL;
		if (e && e->type->ops.prepare_request) {
			if (e->type->icq_cache)
				blk_mq_sched_assign_ioc(rq);

			e->type->ops.prepare_request(rq, bio);
			rq->rq_flags |= RQF_ELVPRIV;
		}
	}
	data->hctx->queued++;
	return rq;
}


//**************************** request派发 ********************************
bool blk_mq_run_hw_queue(struct blk_mq_hw_ctx *hctx, bool async)
{
	int srcu_idx;
	bool need_run;

	/*
	 * When queue is quiesced, we may be switching io scheduler, or
	 * updating nr_hw_queues, or other things, and we can't run queue
	 * any more, even __blk_mq_hctx_has_pending() can't be called safely.
	 *
	 * And queue will be rerun in blk_mq_unquiesce_queue() if it is
	 * quiesced.
	 */
	hctx_lock(hctx, &srcu_idx);
	need_run = !blk_queue_quiesced(hctx->queue) &&
		blk_mq_hctx_has_pending(hctx);
	hctx_unlock(hctx, srcu_idx);
	// 有无可以派发的rq
	if (need_run) {
		__blk_mq_delay_run_hw_queue(hctx, async, 0);
		return true;
	}

	return false;
}
EXPORT_SYMBOL(blk_mq_run_hw_queue);

static void __blk_mq_delay_run_hw_queue(struct blk_mq_hw_ctx *hctx, bool async,
					unsigned long msecs)
{
	if (unlikely(blk_mq_hctx_stopped(hctx)))
		return;

	if (!async && !(hctx->flags & BLK_MQ_F_BLOCKING)) {
		int cpu = get_cpu();
		if (cpumask_test_cpu(cpu, hctx->cpumask)) {
			//同步派发且当前CPU的软件队列映射到该硬件队列
			__blk_mq_run_hw_queue(hctx);
			put_cpu();
			return;
		}

		put_cpu();
	}

	//启动延时
	kblockd_mod_delayed_work_on(blk_mq_hctx_next_cpu(hctx), &hctx->run_work,
				    msecs_to_jiffies(msecs));
}
int kblockd_mod_delayed_work_on(int cpu, struct delayed_work *dwork,
				unsigned long delay)
{
	return mod_delayed_work_on(cpu, kblockd_workqueue, dwork, delay);
}
EXPORT_SYMBOL(kblockd_mod_delayed_work_on);


static void __blk_mq_run_hw_queue(struct blk_mq_hw_ctx *hctx)
{
	int srcu_idx;

	/*
	 * We should be running this queue from one of the CPUs that
	 * are mapped to it.
	 *
	 * There are at least two related races now between setting
	 * hctx->next_cpu from blk_mq_hctx_next_cpu() and running
	 * __blk_mq_run_hw_queue():
	 *
	 * - hctx->next_cpu is found offline in blk_mq_hctx_next_cpu(),
	 *   but later it becomes online, then this warning is harmless
	 *   at all
	 *
	 * - hctx->next_cpu is found online in blk_mq_hctx_next_cpu(),
	 *   but later it becomes offline, then the warning can't be
	 *   triggered, and we depend on blk-mq timeout handler to
	 *   handle dispatched requests to this hctx
	 */
	if (!cpumask_test_cpu(raw_smp_processor_id(), hctx->cpumask) &&
		cpu_online(hctx->next_cpu)) {
		printk(KERN_WARNING "run queue from wrong CPU %d, hctx %s\n",
			raw_smp_processor_id(),
			cpumask_empty(hctx->cpumask) ? "inactive": "active");
		dump_stack();
	}

	/*
	 * We can't run the queue inline with ints disabled. Ensure that
	 * we catch bad users of this early.
	 */
	WARN_ON_ONCE(in_interrupt());

	might_sleep_if(hctx->flags & BLK_MQ_F_BLOCKING);

	hctx_lock(hctx, &srcu_idx);
	blk_mq_sched_dispatch_requests(hctx);
	hctx_unlock(hctx, srcu_idx);
}
