#include "uthash.h"
#include <linux/perf_event.h>
#include <pthread.h>


typedef uint64_t u64;

#define WEIGHT_BUCKET_INTERVAL 50
#define WEIGHT_BUCKETS_NR 19
#define NUMATOOL_ERROR	0
#define NUMATOOL_SUCCESS	1
#define DEFAULT_MEASURE_TIME	45
#define IP_NUM	32
//NUMBER OF PERFORMANCE counters to use
#define COUNT_NUM   2
//number of samples in cirbular buffer
#define BUFFER_SIZE					500
typedef enum {
	B_FALSE = 0,
	B_TRUE
} boolean_t;

typedef struct _count_value {
	uint64_t counts[COUNT_NUM];
} count_value_t;

typedef struct _perf_cpu {
	int cpuid;
	int fds[COUNT_NUM];
	int group_idx;
	int map_len;
	int map_mask;
	void *map_base;
	boolean_t hit;
	boolean_t hotadd;
	boolean_t hotremove;
	count_value_t countval_last;
} perf_cpu_t;

typedef struct _pf_ll_rec {
	unsigned int pid;
	unsigned int tid;
	uint64_t addr;
	uint64_t cpu;
	uint64_t latency;
	unsigned int ip_num;
	uint64_t ips[IP_NUM];
	union perf_mem_data_src data_source;
} pf_ll_rec_t;


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

struct l3_addr{
	void* page_addr;
	struct l3_addr *next;
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
	int migrate_chunk_size;
	const char** command2_launch;
	int	argv_size;
	int measure_time;
	perf_cpu_t *cpus_ll;
	perf_cpu_t *cpus_pf;
	boolean_t end_recording;
	struct l3_addr *pages_2move;
	int number_pages2move;
	int moved_pages;
	struct sampling_metrics metrics;	
};


void add_mem_access( struct sampling_settings *ss, void *page_addr, int accessing_cpu);
void add_lvl_access( struct sampling_settings *ss, union perf_mem_data_src *entry, int weight );
void print_statistics(struct sampling_settings *ss);
int filter_local_accesses(union perf_mem_data_src *entry);
