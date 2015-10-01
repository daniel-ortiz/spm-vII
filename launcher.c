/*
 * launcher.c
 * 
 * Copyright 2015 Do <do@nux>
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 * 
 * untitled
 */

#include <inttypes.h>
#include <sys/types.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <linux/perf_event.h>
#include <unistd.h>
#include <sys/sysinfo.h>
#include <linux/unistd.h>

#define COUNT_NUM   5
#define IP_NUM	32
#define KERNEL_ADDR_START	0xffffffff80000000
#define INVALID_FD	-1
#define INVALID_CONFIG	(uint64_t)(-1)
#define PF_MAP_NPAGES_MAX           1024
#define PF_MAP_NPAGES_MIN           64
#define PF_MAP_NPAGES_NORMAL        256
#define BUFFER_SIZE					500

#define rmb() asm volatile("lock; addl $0,0(%%esp)" ::: "memory")

typedef enum {
	PRECISE_NORMAL = 0,
	PRECISE_HIGH,
	PRECISE_LOW
} precise_type_t;

typedef struct _count_value {
	uint64_t counts[COUNT_NUM];
} count_value_t;

typedef enum {
	B_FALSE = 0,
	B_TRUE
} boolean_t;

typedef enum {
	COUNT_INVALID = -1,
	COUNT_CORE_CLK = 0,
	COUNT_RMA,
	COUNT_CLK,
	COUNT_IR,
	COUNT_LMA
} count_id_t;

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

typedef struct _pf_conf {
	count_id_t count_id;
	uint32_t type;
	uint64_t config;
	uint64_t config1;
	uint64_t sample_period;
} pf_conf_t;

static const char * const mem_lvl[] = {
	"N/A",
	"HIT",
	"MISS",
	"L1",
	"LFB",
	"L2",
	"L3",
	"Local RAM",
	"Remote RAM (1 hop)",
	"Remote RAM (2 hops)",
	"Remote Cache (1 hop)",
	"Remote Cache (2 hops)",
	"I/O",
	"Uncached",
};
#define NUM_MEM_LVL (sizeof(mem_lvl)/sizeof(const char *))

precise_type_t g_precise;
static int s_mapsize, s_mapmask;
int g_pagesize;

pagesize_init(void)
{
	g_pagesize = getpagesize();
}

pf_ringsize_init(void)
{
	switch (g_precise) {
	case PRECISE_HIGH:
		s_mapsize = g_pagesize * (PF_MAP_NPAGES_MAX + 1);
		s_mapmask = (g_pagesize * PF_MAP_NPAGES_MAX) - 1;
		break;
		
	case PRECISE_LOW:
		s_mapsize = g_pagesize * (PF_MAP_NPAGES_MIN + 1);
		s_mapmask = (g_pagesize * PF_MAP_NPAGES_MIN) - 1;
		break;

	default:
		s_mapsize = g_pagesize * (PF_MAP_NPAGES_NORMAL + 1);
		s_mapmask = (g_pagesize * PF_MAP_NPAGES_NORMAL) - 1;
		break;	
	}

	return (s_mapsize - g_pagesize);
}

char* print_access_type(int entry)
{
	char* out;
	size_t sz = 500 - 1; /* -1 for null termination */
	size_t i, l = 0;
	unsigned int m =  PERF_MEM_LVL_NA;
	unsigned int hit, miss;
	out=malloc(500*sizeof(char));

	m  = (unsigned int)entry;

	out[0] = '\0';

	hit = m & PERF_MEM_LVL_HIT;
	miss = m & PERF_MEM_LVL_MISS;

	/* already taken care of */
	m &= ~(PERF_MEM_LVL_HIT|PERF_MEM_LVL_MISS);

	for (i = 0; m && i < NUM_MEM_LVL; i++, m >>= 1) {
		if (!(m & 0x1))
			continue;
		if (l) {
			strcat(out, " or ");
			l += 4;
		}
		strncat(out, mem_lvl[i], sz - l);
		l += strlen(mem_lvl[i]);
	}
	if (*out == '\0')
		strcpy(out, "N/A");
	if (hit)
		strncat(out, " hit", sz - l);
	if (miss)
		strncat(out, " miss", sz - l);

	return  out;
}

ll_recbuf_update(pf_ll_rec_t *rec_arr, int *nrec, pf_ll_rec_t *rec)
{
	int i;

	if ((rec->pid == 0) || (rec->tid == 0)) {
		/* Just consider the user-land process/thread. */
		return;	
	}

	/*
	 * The size of array is enough.
	 */
	i = *nrec;
	memcpy(&rec_arr[i], rec, sizeof (pf_ll_rec_t));
	
	if(*nrec == BUFFER_SIZE-1){
		*nrec=0;
	}else{
		*nrec += 1;
	}
	
}

 static void mmap_buffer_skip(struct perf_event_mmap_page *header, int size)
{
	int data_head;

	data_head = header->data_head;
	rmb();

	if ((header->data_tail + size) > data_head) {
		size = data_head - header->data_tail;
	}

	header->data_tail += size;
}

static int
mmap_buffer_read(struct perf_event_mmap_page *header, void *buf, size_t size)
{
	void *data;
	uint64_t data_head, data_tail;
	int data_size, ncopies;
	
	/*
	 * The first page is a meta-data page (struct perf_event_mmap_page),
	 * so move to the second page which contains the perf data.
	 */
	data = (void *)header + g_pagesize;

	/*
	 * data_tail points to the position where userspace last read,
	 * data_head points to the position where kernel last add.
	 * After read data_head value, need to issue a rmb(). 
	 */
	data_tail = header->data_tail;
	data_head = header->data_head;
	//DO: I had to comment the following because it generates a segmentation fault
	//asm volatile("lock; addl $0,0(%%esp)" ::: "memory");

	/*
	 * The kernel function "perf_output_space()" guarantees no data_head can
	 * wrap over the data_tail.
	 */
	if ((data_size = data_head - data_tail) < size) {
		return (-1);
	}

	data_tail &= s_mapmask;

	/*
	 * Need to consider if data_head is wrapped when copy data.
	 */
	if ((ncopies = (s_mapsize - data_tail)) < size) {
		memcpy(buf, data + data_tail, ncopies);
		memcpy(buf + ncopies, data, size - ncopies);
	} else {
		memcpy(buf, data + data_tail, size);
	}

	header->data_tail += size;	
	return (0);
}


static int
ll_sample_read(struct perf_event_mmap_page *mhdr, int size,
	pf_ll_rec_t *rec)
{
	struct { uint32_t pid, tid; } id;
	uint64_t addr, cpu, weight, nr, value, *ips,origin,time,ip,period;
	int i, j, ret = -1;
	union perf_mem_data_src dsrc;

	/*
	 * struct read_format {
	 *	{ u32	pid, tid; }
	 *	{ u64	addr; }
	 *	{ u64	cpu; }
	 *	[ u64	nr; }
	 *	{ u64   ips[nr]; }
	 *	{ u64	weight; }
	 * };
	 */
	
	if (mmap_buffer_read(mhdr, &ip, sizeof (ip)) == -1) {
		printf( "ll_sample_read: read ip failed.\n");
		goto L_EXIT;
	}
	size -= sizeof (ip);
	
	if (mmap_buffer_read(mhdr, &id, sizeof (id)) == -1) {
		printf("ll_sample_read: read pid/tid failed.\n");
		goto L_EXIT;
	}

	size -= sizeof (id);
	
	if (mmap_buffer_read(mhdr, &time, sizeof (time)) == -1) {
		printf("ll_sample_read: read time failed.\n");
		goto L_EXIT;
	}
	
	size -= sizeof (time);
	if (mmap_buffer_read(mhdr, &addr, sizeof (addr)) == -1) {
		printf("ll_sample_read: read addr failed.\n");
		goto L_EXIT;
	}
	
	size -= sizeof (addr);

	if (mmap_buffer_read(mhdr, &cpu, sizeof (cpu)) == -1) {
		printf( "ll_sample_read: read cpu failed.\n");
		goto L_EXIT;
	}
	size -= sizeof (cpu);
	
	if (mmap_buffer_read(mhdr, &period, sizeof (period)) == -1) {
		printf( "ll_sample_read: read period failed.\n");
		goto L_EXIT;
	}
	size -= sizeof (period);
	
	if (mmap_buffer_read(mhdr, &weight, sizeof (weight)) == -1) {
		printf("ll_sample_read: read weight failed.\n");
		goto L_EXIT;
	}
	
	size -= sizeof (weight);

	
	if (mmap_buffer_read(mhdr, &dsrc, sizeof (dsrc)) == -1) {
		printf("ll_sample_read: read origin failed.\n");
		goto L_EXIT;
	}
	//printf("%d %s %lu %d %lu -",dsrc.mem_lvl, print_access_type(dsrc.mem_lvl),weight,id.pid, dsrc);
	
	size -= sizeof (dsrc);
	
	rec->ip_num = j;
	rec->pid = id.pid;
	rec->tid = id.tid;
	rec->addr = addr;
	rec->cpu = cpu;
	rec->latency = weight;
	rec->data_source=dsrc;	
	ret = 0;

L_EXIT:
	if (size > 0) {
		mmap_buffer_skip(mhdr, size);
		printf( "ll_sample_read: skip %d bytes, ret=%d\n",
			size, ret);
	}

	return (ret);
}

static int
pf_event_open(struct perf_event_attr *attr, pid_t pid, int cpu, int group_fd,
	unsigned long flags)
{
	return (syscall(__NR_perf_event_open, attr, pid, cpu, group_fd, flags));
}


int pf_ll_setup(struct _perf_cpu *cpu)
{
	struct perf_event_attr attr;
	int *fds = cpu->fds;

	memset(&attr, 0, sizeof (attr));
	attr.type = 4;
	attr.config = 461;
	attr.config1 = 148;
	attr.sample_period = 500;
	attr.precise_ip = 1;
	attr.exclude_guest = 1;
	attr.sample_type = PERF_SAMPLE_IP |PERF_SAMPLE_TID | PERF_SAMPLE_TIME | PERF_SAMPLE_ADDR | PERF_SAMPLE_CPU |
		PERF_SAMPLE_PERIOD | PERF_SAMPLE_WEIGHT | PERF_SAMPLE_DATA_SRC  ; 
	attr.disabled = 1;

	if ((fds[0] = pf_event_open(&attr, -1, cpu->cpuid, -1, 0)) < 0) {
		printf("pf_ll_setup: pf_event_open is failed for CPU%d\n", cpu->cpuid);
		fds[0] = INVALID_FD;
		return (-1);
	}
	
	if ((cpu->map_base = mmap(NULL, s_mapsize, PROT_READ | PROT_WRITE,
		MAP_SHARED, fds[0], 0)) == MAP_FAILED) {
		close(fds[0]);
		fds[0] = INVALID_FD;
		return (-1);
	}

	cpu->map_len = s_mapsize;
	cpu->map_mask = s_mapmask;
	return (0);
}


void
pf_ll_record(struct _perf_cpu *cpu, pf_ll_rec_t *rec_arr, int *nrec)
{
	struct perf_event_mmap_page *mhdr = cpu->map_base;
	struct perf_event_header ehdr;
	pf_ll_rec_t rec;
	int size;

	if (nrec == NULL) {
		printf("erroneous reference to record position");
	}

	for (;;) {
		if (mmap_buffer_read(mhdr, &ehdr, sizeof(ehdr)) == -1) {
			/* No valid record in ring buffer. */
   	    	return;
 		}
	
		if ((size = ehdr.size - sizeof (ehdr)) <= 0) {
			return;
		}

		if ((ehdr.type == PERF_RECORD_SAMPLE) && (rec_arr != NULL)) {
			if (ll_sample_read(mhdr, size, &rec) == 0) {
				ll_recbuf_update(rec_arr, nrec, &rec);
			} else {
				/* No valid record in ring buffer. */
				return;	
			}
		} else {
			mmap_buffer_skip(mhdr, size);
		}
		//printf("read sample %d value %d ",*nrec,rec_arr[*nrec].latency, ehdr.size);
	}
}

int
pf_ll_start(struct _perf_cpu *cpu)
{
	if (cpu->fds[0] != INVALID_FD) {
		return (ioctl(cpu->fds[0], PERF_EVENT_IOC_ENABLE, 0));
	}
	
	return (0);
}

int main(int argc, char **argv)
{
	pagesize_init();
	g_precise=PRECISE_HIGH;
	pf_ringsize_init();
	
	perf_cpu_t *cpus= malloc(sizeof(perf_cpu_t)*32);
	
	for(int i=0; i<32; i++){
		memset((cpus+i),0,sizeof(perf_cpu_t));
		cpus[i].cpuid=i;
		pf_ll_setup((cpus+i));
	}
	
	for(int i=0; i<32; i++){
		pf_ll_start((cpus+i));
	}
	sleep(1);
	pf_ll_rec_t* record=malloc(sizeof(pf_ll_rec_t)*BUFFER_SIZE);
	int nrec=0;
	int bef=0, wr_diff=0,current;
	for(;;){
	readagain:	for(int i=0; i<32; i++){
			bef=nrec;
			pf_ll_record((cpus+i),record,&nrec);
			wr_diff=nrec >= bef ? nrec > bef : nrec+BUFFER_SIZE-bef;
			
			while(wr_diff>0){
				current=nrec-wr_diff;
				current = current < 0 ? BUFFER_SIZE+current : current;
				//here we suppose we consume the sample
				printf("%lu %d %d *", (record + current)->latency, nrec, current);
				current=current != BUFFER_SIZE-1 ? current++ : 0;
				wr_diff--;
			}
	}
	}
	printf("fin\n");
	return 0;
}

