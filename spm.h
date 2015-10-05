#include "uthash.h"
#include <linux/perf_event.h>
#include <pthread.h>

typedef uint64_t u64;

#define WEIGHT_BUCKET_INTERVAL 50
#define WEIGHT_BUCKETS_NR 19

struct freq_stats{
	int count;
	int freq;
	UT_hash_handle hh;
};

struct page_stats{
	int *proc_accesses;
	void* page_addr;
	UT_hash_handle hh;
};

struct access_stats{
	int count;
	int mem_lvl;
	UT_hash_handle hh;
};

struct sampling_metrics {
	int total_samples;
	int *process_samples;
	struct page_stats *page_accesses;
	struct access_stats *lvl_accesses;
	struct freq_stats *freq_accesses;
	int access_by_weight[WEIGHT_BUCKETS_NR];
	};
	
 struct sampling_settings{
	int	pid_uo;
	int n_cpus;
	int n_cores;
	int *core_to_cpu;
	struct sampling_metrics metrics;	
};

