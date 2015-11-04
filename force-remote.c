


#define _GNU_SOURCE
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <dirent.h>
#include <string.h>
#include <sched.h>
#include "spm.h"

 static int cpu0[16]={0,1,2,3,4,5,6,7,16,17,18,19,20,21,22,23};
 static int cpu1[16]={8,9,10,11,12,13,14,15,24,25,26,27,28,29,30,31};
 static int core_to_cpu[32]={0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1};
  
struct running_thread{
	int thread_id;
	int running_cpu;
	struct running_thread *nextrt;
};


int get_opposite_core(int ncore){

		int position=-1,end=0,iserr=0;
		//find core in the array
		int current_cpu=core_to_cpu[ncore];
		int* arr= current_cpu==0 ? cpu0 : cpu1; 
		for(int i=0; i<16 && !end; i++){
			if(arr[i]==ncore){
					position=i;
					end=1;
			}	
		}
		if(position <0){
				printf("cpu not found");
				return -1;
		}
		int assigned_core= current_cpu==0 ? cpu1[position] : cpu0[position] ;
		
		return assigned_core;
}


int get_child_threads( int* child_threads,int pid){
	char dir_path[30],strpid[6],spid;
	int *pids,tcount=0;
	DIR *tasksdir;
	struct dirent *dirp;
	
	const char* path= "/proc/";
	const char* path2="/task/";
	
	pids=child_threads;
	sprintf(dir_path,"%s",path);
	sprintf(strpid,"%d",pid);
	if(pid<0) {
		printf("wrong pid");
		return -1;
	}
	strcat(dir_path,strpid);
	strcat(dir_path,path2);
	printf("%s \n",dir_path);
	
	tasksdir=opendir(dir_path);
	if(!tasksdir){
			printf("could not open dir");
			return -1;
	}
	
	
	while(dirp= readdir(tasksdir)){
		printf("%s \n", dirp->d_name);
		if(atoi(dirp->d_name)>0){
			pids[tcount]=atoi(dirp->d_name);
			tcount++;
		}
	}
	//*child_threads=pids;
	
	return tcount;
	
}

int getcpu_fromset(cpu_set_t set, int max_cpus){
	int j;
	
	for(j=0; j<max_cpus;j++){
		if(CPU_ISSET(j,&set)) return j;
		
	}
	
	return -1;
} 

void force_remote(int pid)
{
	int num_threads,child_threads[100];
	int i;
	cpu_set_t set;
	int curr_thread,opposite_core;
	int ret;
	int max_cores=(int)sysconf(_SC_NPROCESSORS_ONLN);
	int host_core;
	struct running_thread *startrt, *currentrt,*previousrt;
	printf("system with %d cores will watch pid %d \n",max_cores,pid);
	//pid=launch_command((const char **)&argv[1],argc-1);
	startrt=NULL;
	previousrt=NULL;
	currentrt=NULL;
	sleep(2);
	if(pid>0) num_threads=get_child_threads(child_threads,pid);
	
	if(num_threads<1) {
		printf("Threads were not found \n");
		return ;
	}
	
	for(i=0; i<num_threads;i++){

		curr_thread=child_threads[i];
		
		if(sched_getaffinity(curr_thread, sizeof(set), &set)==-1) {
				printf("get affinity error tid %d \n", child_threads[i]);
				return ;
		}else{
			if(currentrt) previousrt=currentrt;
			host_core=getcpu_fromset(set,max_cores);
			printf("affinity %d %d %d\n",host_core,  curr_thread, child_threads[i]);	
			if(host_core<0){
					printf("Found no core for thread %d",curr_thread);
			}
			currentrt=malloc(sizeof(struct running_thread));
			memset(currentrt,0,sizeof(struct running_thread));
			currentrt->thread_id=curr_thread;
			currentrt->running_cpu=host_core;
			if(!startrt) startrt=currentrt;
			if(previousrt) previousrt->nextrt=currentrt;
		}
	}
	currentrt=startrt;
	sleep(15);
	while(currentrt){
		opposite_core=get_opposite_core(currentrt->running_cpu);
		printf("will move thread %d from core %d to core %d \n",currentrt->thread_id, currentrt->running_cpu, opposite_core);
		CPU_ZERO(&set);
		CPU_SET(opposite_core, &set);
		if (sched_setaffinity(currentrt->thread_id, sizeof(set), &set) == -1)
			printf("%s", "could not pin");
				
		currentrt=currentrt->nextrt;
	}
	printf("Control passed to SPM\n");
	//Will find a thread on the other processor for that thread and pin it there

}


