
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
 #include <numa.h>
#include "spm.h"

int wait_watch_process(int seconds,struct sampling_settings* ss){
	int i=0,exit=0;
	int sigres,*st=0,errn;

	pf_profiling_rec_t* pf_record=malloc(sizeof(pf_profiling_rec_t)*BUFFER_SIZE);

	//every second it wakes up to make sure the process is alive
	while(!exit){
		if(ss->pf_measurements){
			read_pf_samples(ss,pf_record );
			calculate_pf_diff(ss);
		}
		sleep(1);
		sigres=kill(ss->pid_uo,0);
		waitpid(ss->pid_uo,st,WNOHANG);
		if(sigres==-1 ){
			errn=errno;
			if (errn== ESRCH || errn==ECHILD){
					return -1;
				}
			}
		exit= (seconds > 1 && ++i<seconds) || seconds < 1 ? 0 : 1;
	}
	return 0;
}


void *control_spm (void *arg){
	int measuring_time;
	struct sampling_settings *ss=(struct sampling_settings*  ) arg;
	struct sampling_metrics old_sm;
	printf("MIG-CTRL> begin of mesurement control \n");
	
	measuring_time=ss->measure_time > 0 ? ss->measure_time : DEFAULT_MEASURE_TIME ;
	
	ss->start_time=wtime();
	int wait_res= wait_watch_process( measuring_time,ss);
	
	if(wait_res) goto end_noproc;
	
	print_statistics(ss);
	
	if(ss->only_sample){
		kill(ss->pid_uo,9);
		printf("MIG-CTRL> will end because it is only a measurement run \n");
		stop_sampling(ss);
		ss->end_recording=1;
		return 0;
	}
	print_performance(ss);
	
	do_great_migration(ss);

	printf("** %d\n",ss->number_pages2move);
	
	old_sm=ss->metrics;
	//temporarily disabled because of sporadic segfaults
	//free_metrics(&old_sm);
	

	struct sampling_metrics *sm=malloc(sizeof(struct sampling_metrics));
	memset(sm,0,sizeof(struct sampling_metrics));
	sm->process_samples=malloc(sizeof(int)*ss->n_cores);
	sm->remote_samples=malloc(sizeof(int)*ss->n_cores);
	memset(sm->process_samples,0,sizeof(int)*ss->n_cores);
	memset(sm->remote_samples,0,sizeof(int)*ss->n_cores);
	sm->pf_last_values=malloc(COUNT_NUM*sizeof(u64)*ss->n_cores);
	sm->pf_read_values=malloc(COUNT_NUM*sizeof(u64)*ss->n_cores);
	sm->pf_diff_values=malloc(COUNT_NUM*sizeof(u64)*ss->n_cores);
	sm->perf_info_first=malloc(sizeof(struct perf_info*)*ss->n_cores);
	sm->perf_info_last=malloc(sizeof(struct perf_info*)*ss->n_cores);
	memset(sm->pf_last_values,0,COUNT_NUM*sizeof(u64)*ss->n_cores);
	memset(sm->pf_read_values,0,COUNT_NUM*sizeof(u64)*ss->n_cores);
	memset(sm->pf_diff_values,0,COUNT_NUM*sizeof(u64)*ss->n_cores);
	memset(sm->perf_info_first,0,sizeof(struct perf_info*)*ss->n_cores);
	memset(sm->perf_info_last,0,sizeof(struct perf_info*)*ss->n_cores);
	ss->metrics=*sm;
	
	
	
	printf("MIG-CTRL> migration complete \n");
	//reenable sampling
	start_sampling(ss);
	//we wait until the process finishes
	wait_res=wait_watch_process(-1,ss);
	stop_sampling(ss);
	ss->end_recording=1;
	print_statistics(ss);
	print_performance(ss);
	end_noproc:
	printf("MIG-CTRL> End of sampling due to end of existing process  \n");
	fflush(stdout);
	

}


//this is the main thread which itself will spawn another thread
void* run_numa_sampling(void *arg){
	pthread_t control_thread;
	struct sampling_settings *ss=(struct sampling_settings* ) arg;
	
	//circular buffer for storing the load latency samples
	pf_ll_rec_t* ll_record=malloc(sizeof(pf_ll_rec_t)*BUFFER_SIZE);

	ss->end_recording=0;
	//launch control thread
	if(pthread_create(&control_thread,NULL,control_spm,ss)){
		printf("MIG-CTRL> could not create aux thread \n");
	}
	
	//will start recording samples
	read_ll_samples(ss,ll_record);
	pthread_join(control_thread, NULL); 
	printf("MIG-CTRL> sampling ended \n");
}


int init_spm(struct sampling_settings *ss){
	pthread_t spm_thread;
	int launched_pid;
	//struct sampling_metrics old_sm;
	struct cpu_topo *cpu_topo;
	//required by numatop core
	init_globals();
	
	cpu_topo=build_cpu_topology();
	ss->n_cores=numa_num_configured_cpus();
	ss->n_cpus=numa_num_configured_nodes();
	
	if(ss->ll_sampling_period<3){
		ss->ll_sampling_period=DEFAULT_LL_SAMPLING_PERIOD;
	}
	
	if(ss->ll_weight_threshold<3){
		ss->ll_weight_threshold=DEFAULT_LL_WEIGHT_THRESHOLD;
	}
	
	perf_cpu_t *cpus_ll= malloc(sizeof(perf_cpu_t)*ss->n_cores);
	perf_cpu_t *cpus_pf= malloc(sizeof(perf_cpu_t)*ss->n_cores);
	
	//create measurement information acording to core and numa node count
	memset(&(ss->metrics),0,sizeof(struct sampling_metrics));
	ss->metrics.process_samples=malloc(sizeof(int)*ss->n_cores);
	ss->metrics.remote_samples=malloc(sizeof(int)*ss->n_cores);
	ss->metrics.pf_last_values=malloc(COUNT_NUM*sizeof(u64)*ss->n_cores);
	ss->metrics.pf_read_values=malloc(COUNT_NUM*sizeof(u64)*ss->n_cores);
	ss->metrics.pf_diff_values=malloc(COUNT_NUM*sizeof(u64)*ss->n_cores);
	ss->metrics.perf_info_first=malloc(sizeof(struct perf_info*)*ss->n_cores);
	ss->metrics.perf_info_last=malloc(sizeof(struct perf_info*)*ss->n_cores);
	memset(ss->metrics.perf_info_first,0,sizeof(struct perf_info*)*ss->n_cores);
	memset(ss->metrics.perf_info_last,0,sizeof(struct perf_info*)*ss->n_cores);
	memset(ss->metrics.pf_last_values,0,COUNT_NUM*sizeof(u64)*ss->n_cores);
	memset(ss->metrics.pf_read_values,0,COUNT_NUM*sizeof(u64)*ss->n_cores);
	memset(ss->metrics.pf_diff_values,0,COUNT_NUM*sizeof(u64)*ss->n_cores);
	memset(ss->metrics.process_samples,0,sizeof(int)*ss->n_cores);
	memset(ss->metrics.remote_samples,0,sizeof(int)*ss->n_cores);
	ss->core_to_cpu=malloc(sizeof(int)*ss->n_cores);
	memset(ss->core_to_cpu,0,ss->n_cores*sizeof(int));
	init_processor_mapping(ss, cpu_topo);
	ss->cpus_ll=cpus_ll;
	ss->cpus_pf=cpus_pf;
	
	//Set up registers and start before taking samples
	
	setup_sampling(ss);
	start_sampling(ss);

	//launches process if no existing process is specified
	if( ss->pid_uo==-1){
		printf("MIG-CTRL> will launch external executable\n");
		ss->pid_uo=launch_command(ss->command2_launch, ss->argv_size);
	}else{
		printf("MIG-CTRL> will watch pid %d\n",ss->pid_uo);
	}
	
	if(pthread_create(&spm_thread,NULL,&run_numa_sampling, ss)){
		return NUMATOOL_ERROR; 
	}
	
	pthread_join(spm_thread, NULL); 
		return NUMATOOL_SUCCESS;
		
}

//Define standalone if an standalone version of the program is to be made
//if not it will be embedded in another program with its own main
#ifdef STANDALONE
int main(int argc, char **argv)
{	struct sampling_settings st;
	int pid,period,wmin,mtime;
	const char* cmd;
	char* lbl;
	memset(&st,0,sizeof(struct sampling_settings));
	st.pid_uo= -1; 
	st.only_sample=0;
	st.pf_measurements=0;
	if (argc < 3){
			printf("MIG-CTRL> missing arguments \n");
			return 0;
	}
	
	if ( !strcmp(argv[1],"-pid") ){
		pid =  atoi(argv[2])>0 ? atoi(argv[2]) : -1;
		st.pid_uo= pid; 
	}
	
	else if ( !strcmp(argv[1],"-cmd") && argv[2] ){
		st.command2_launch = (const char**)&argv[2];
		st.argv_size=argc-2;	
	}
	
	 if ( argc > 2 && !strcmp(argv[1],"-per") && argv[2] ){
		period=atoi(argv[2]);
		if(period > 0){
			st.ll_sampling_period=period;
		}
	}
	
	 if ( argc > 4 && !strcmp(argv[3],"-wmin") && argv[4] ){
		wmin=atoi(argv[4]);
		if(wmin > 0){
			st.ll_weight_threshold=wmin;
		}
	}
	
	 if ( argc > 6 && !strcmp(argv[5],"-mtime") && argv[6] ){
		mtime=atoi(argv[6]);
		if(lbl > 0){
			st.measure_time=mtime;
		}
	}
	
	 if ( argc > 8 && !strcmp(argv[7],"-lbl") && argv[8] ){
		lbl=argv[8];
		if(strlen(lbl) > 0){
			st.output_label=lbl;
		}
	}
	if ( argc > 9 && !strcmp(argv[9],"-onlysample")  ){
		st.only_sample=1;
	}
	
	if ( argc > 10 && !strcmp(argv[10],"-profilingsamples")  ){
		st.pf_measurements=1;
	}

	
	 if ( argc > 11 && !strcmp(argv[11],"-cmd") && argv[12] ){
		st.command2_launch = (const char**)&argv[12];
		st.argv_size=argc-12;
		
	}
	if ( argc > 11 && !strcmp(argv[11],"-pid") && argv[12] ){
		pid =  atoi(argv[12])>0 ? atoi(argv[12]) : -1;
		st.pid_uo= pid; 
		st.argv_size=argc-12;
	}
	

	init_spm(&st);
	return 0;
	 
}
#endif
