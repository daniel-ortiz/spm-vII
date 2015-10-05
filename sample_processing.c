
#include <stdlib.h>
#include <stdio.h>
#include "spm.h"


void add_mem_access( struct sampling_settings *ss, void *page_addr, int accessing_cpu){
	struct page_stats *current=NULL; 
	struct page_stats *cursor=NULL; 
	int proc;

	//search the node for apparances

	HASH_FIND_PTR(ss->metrics.page_accesses, &page_addr,current);
	proc=ss->core_to_cpu[accessing_cpu];
	if (current==NULL){
		current=malloc(sizeof(struct page_stats));
		//one position created for every numa node
		current->proc_accesses=malloc(sizeof(int)*ss->n_cpus);
		memset(current->proc_accesses,0,sizeof(int)*ss->n_cpus);
		
		current->page_addr=page_addr;
		
		HASH_ADD_PTR(ss->metrics.page_accesses,page_addr,current);
		//printf ("add %d \n",HASH_COUNT(ss->metrics.page_accesses));
		
	}

	/*Here begins the new page access bookkeeping*/
	current->proc_accesses[proc]++;
	//printf("%d", proc);
}

void add_lvl_access( struct sampling_settings *ss, union perf_mem_data_src *entry, int weight ){
	struct access_stats *current=NULL;
	int key=(int)entry->mem_lvl;
	int weight_bucket=weight / WEIGHT_BUCKET_INTERVAL;
	
	HASH_FIND_INT(ss->metrics.lvl_accesses, &key,current);
	weight_bucket=weight_bucket > (WEIGHT_BUCKETS_NR-1) ? WEIGHT_BUCKETS_NR-1 : weight_bucket;
	
	ss->metrics.access_by_weight[weight_bucket]++;
	if(!current){
		current=malloc(sizeof(struct access_stats));
		current->count=0;
		current->mem_lvl=(int)entry->mem_lvl;
		HASH_ADD_INT(ss->metrics.lvl_accesses,mem_lvl,current);
	}
	
	current->count++;
	
}

static int freq_sort(struct freq_stats *a, struct freq_stats *b) {
    return a->freq - b->freq;
}

long id_sort(struct page_stats *a, struct page_stats *b) {
    long rst= (long )a->page_addr - (long)b->page_addr;
    return rst;
}


void add_freq_access(struct sampling_settings *ss, int frequency){
	struct freq_stats *current=NULL; 
	HASH_FIND_INT(ss->metrics.freq_accesses, &frequency,current);
	
	if(!current){
			current=malloc(sizeof(struct freq_stats));
			memset(current,0,sizeof(struct freq_stats));
			current->freq=frequency;
			HASH_ADD_INT( ss->metrics.freq_accesses, freq, current );
	}
	
	current->count++;
	
	
}

void print_statistics(struct sampling_settings *ss){
		struct sampling_metrics m=ss->metrics;
		struct page_stats *current,*tmp;
		struct freq_stats *crr,*tmpf;	
		printf("ts %d \n", m.total_samples);
		int i,freq;
		int ubound,lbound;
		char * lbl="TODO-setlabel";
		
		for(i=0; i<ss->n_cores;i++){
				printf("cpu %d:  %d \n",  i, m.process_samples[i]);
		}
		
		for(i=0; i<WEIGHT_BUCKETS_NR; i++){
			lbound=i*WEIGHT_BUCKET_INTERVAL;
			ubound=(i+1)*WEIGHT_BUCKET_INTERVAL;
			printf("%s :%d-%d: %d\n", lbl,lbound,ubound, ss->metrics.access_by_weight[i]);
		}
	
		HASH_SORT( ss->metrics.page_accesses, id_sort );
		
		HASH_ITER(hh, ss->metrics.page_accesses, current, tmp) {
			freq=0;
				for(i=0; i<ss->n_cpus; i++){
					freq+=current->proc_accesses[i];
				}
				//printf("record ");
			add_freq_access(ss,freq);	
		}
		
		HASH_SORT( ss->metrics.freq_accesses, freq_sort );
		
		HASH_ITER(hh, ss->metrics.freq_accesses, crr, tmpf) {
			printf("%s:pages accessed:%d:%d \n",lbl,crr->freq,crr->count);
		}
	}
