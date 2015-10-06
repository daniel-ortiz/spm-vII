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


void add_mem_access( struct sampling_settings *ss, void *page_addr, int accessing_cpu);
void add_lvl_access( struct sampling_settings *ss, union perf_mem_data_src *entry, int weight );
void print_statistics(struct sampling_settings *ss);
int filter_local_accesses(union perf_mem_data_src *entry);
