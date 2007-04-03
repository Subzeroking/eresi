/*
** threads.c for e2dbg
**
** Hook the threads API for keeping thread debugger information accurately up2date
**
** Last Update Thu July 06 19:37:26 2006 mm
**
** $Id: threads.c,v 1.6 2007-04-02 18:00:31 may Exp $
**
*/
#include "libe2dbg.h"
#include <pthread.h>


/* Hooked start routine for all threads */
static void*		e2dbg_thread_start(void *param)
{
  e2dbgthread_t		*cur;
  //e2dbgthread_t		*stopped;
  void*			(*start)(void *param);
  char			key[15];
  pthread_attr_t	attr;
  int			ret;
#if __DEBUG_THREADS__
  char			logbuf[BUFSIZ];
#endif

  sigset_t		mask;
  sigset_t		oldmask;

  /* Do not allow to start a thread while another one is created or breaking */
  SETSIG;
  e2dbg_mutex_lock(&e2dbgworld.dbgbp);
  e2dbg_presence_set();

  /* Set the mask of signals to be handled by this thread */
  sigemptyset (&mask);
  sigaddset (&mask, SIGUSR2);
  sigaddset (&mask, SIGSEGV);
  sigaddset (&mask, SIGBUS);
  sigaddset (&mask, SIGTRAP);
  //sigaddset (&mask, SIGSTOP);
  sigaddset (&mask, SIGINT);
  sigaddset (&mask, SIGILL);
  ret = pthread_sigmask(SIG_UNBLOCK, &mask, &oldmask);

#if __DEBUG_THREADS__
  if (ret)
    fprintf(stderr, "Error while setting sigmask : returned %d \n", ret);
  else
    fprintf(stderr, "Sigmask set succesfully \n");
  e2dbg_output(" [*] BP MUTEX LOCKED [e2dbg_thread_start] \n");
#endif

  snprintf(key, sizeof(key), "%u", (unsigned int) pthread_self());
  cur = hash_get(&e2dbgworld.threads, key);

#if __DEBUG_THREADS__
  fprintf(stderr, " [*] Starting thread ID %u \n", (unsigned int) pthread_self());
#endif

  /* Register the thread as started */
  e2dbgworld.threadnbr++;
  cur->state = E2DBG_THREAD_STARTED;
  if (e2dbgworld.threadnbr == 1)
    cur->initial = 1;

  /* Get stack information for this thread */
#if defined(linux)
  pthread_getattr_np(cur->tid, &attr);
#else
  pthread_attr_get_np(cur->tid, &attr);
#endif

  ret = pthread_attr_getstack(&attr, (void **) &cur->stackaddr, (size_t *) &cur->stacksize);

#if __DEBUG_THREADS__
  if (ret)
    write(1, "Problem during pthread_attr_getstack\n", 37);
  else
    {
      ret = snprintf(logbuf, BUFSIZ, " [D] Thread ID %u has stack at addr %08X with size %u \n",
		     (unsigned int) cur->tid, cur->stackaddr, cur->stacksize);
      write(1, logbuf, ret);
    }
#endif
    
  /* Unlock the mutex */
  start = (void *) cur->entry;

#if __DEBUG_THREADS__
  e2dbg_output(" [*] BP MUTEX UNLOCKED [e2dbg_thread_start] \n");
#endif

  e2dbg_mutex_unlock(&e2dbgworld.dbgbp);

  /* Call the real entry point */
  e2dbg_presence_reset();
  return ((*start)(param));
}


/* Hook for pthread_create */
int		pthread_create (pthread_t *__restrict __threadp,
				__const pthread_attr_t *__restrict __attr,
				void *(*__start_routine) (void *),
				void *__restrict __arg)
{
  e2dbgthread_t	*new;
  char		*key;
  int		ret;
  int		(*fct)(pthread_t *__restrict __threadp, 
		       __const pthread_attr_t *__restrict __attr,
		       void *__start_routine,
		       void *__restrict __arg);

  NOPROFILER_IN();
  e2dbg_presence_set();

  /* Do not allow to create a thread while another one is breaking */
  e2dbg_mutex_lock(&e2dbgworld.dbgbp);

#if __DEBUG_THREADS__
  e2dbg_output(" [*] BP MUTEX LOCKED [pthread_create] \n");
#endif

  fct = (void *) e2dbgworld.syms.pthreadcreate;
  ret = (*fct)(__threadp, __attr, e2dbg_thread_start, __arg);
  XALLOC(__FILE__, __FUNCTION__, __LINE__,new, sizeof(e2dbgthread_t), -1);
  XALLOC(__FILE__, __FUNCTION__, __LINE__,key, 15, -1);
  snprintf(key, 15, "%u", *(unsigned int *) __threadp);
  new->tid   = *(pthread_t *) __threadp;
  new->entry = __start_routine;
  time(&new->stime);
  hash_add(&e2dbgworld.threads, key, new);

  /* Unlock the thread mutex */
#if __DEBUG_THREADS__
  e2dbg_output(" [*] BP MUTEX UNLOCKED [pthread_create] \n");
#endif

  e2dbg_presence_reset();
  e2dbg_mutex_unlock(&e2dbgworld.dbgbp);
  NOPROFILER_ROUT(ret);
}



/* Hook for pthread_exit */
void	pthread_exit(void *retval)
{
  void	(*fct)(void *retval);

  fct = (void *) e2dbgworld.syms.pthreadexit;

#if __DEBUG_THREADS__
  printf(" [D] PTHREAD_EXIT ! Calling back original at %08X \n", (elfsh_Addr) fct);
#endif

  e2dbgworld.threadnbr--;
  (*fct)(retval);
  while (1)
    sleep(1);
}


/* Hook for signal */
/*
__sighandler_t		signal(int signum, __sighandler_t fctptr)
{
  __sighandler_t	(*fct)(int signum, __sighandler_t fctptr);

  fct = (void *) e2dbgworld.signal;

#if __DEBUG_THREADS__
  printf(" [D] THREAD %u SIGNAL (SIGNUM %u) Calling back original at %08X \n", 
  (unsigned int) pthread_self(), signum, (elfsh_Addr) fct);
#endif

  return ((*fct)(signum, fctptr));
}
*/




/* Print all threads state */
void		e2dbg_threads_print()
{
  e2dbgthread_t	*cur;
  u_int         index;
  char		logbuf[BUFSIZ];
  char		*time, *nl, *state, *entry;
  char		c;
  char		**keys;
  int		keynbr;

  keys = hash_get_keys(&e2dbgworld.threads, &keynbr);

  /* Iterate on the threads hash table */
  for (index = 0; index < keynbr; index++)
    {
	cur = hash_get(&e2dbgworld.threads, keys[index]);
	time = ctime(&cur->stime);
	nl = strchr(time, '\n');
	if (nl)
	  *nl = 0x00;
	c = (e2dbgworld.curthread == cur ? '*' : ' ');
	switch (cur->state)
	  {
	  case E2DBG_THREAD_INIT:	/* The thread has not started yet */
	    state = "INIT";
	    break;
	  case E2DBG_THREAD_STARTED:	/* The thread has started */
	    state = "STARTED";
	    break;
	  case E2DBG_THREAD_BREAKING:	/* The thread is processing a breakpoint */
	    state = "BREAKING";
	    break;
	  case E2DBG_THREAD_SIGUSR2:	/* The thread is processing a SIGUSR2 */
	    state = "SIGUSR2";
	    break;
	  case E2DBG_THREAD_BREAKUSR2:	/* The thread is beeing debugged */
	    state = "POSTUSR2";
	    break;
	  case E2DBG_THREAD_STOPPING:	/* The thread is beeing stopped */
	    state = "STOPPING";
	    break;
	  case E2DBG_THREAD_RUNNING:	/* The thread is running, been continued already */
	    state = "RUNNING";
	    break;
	  case E2DBG_THREAD_FINISHED:	/* The thread is finished */
	    state = "FINISHED";
	    break;
	  default:
	    state = "UNKNOWN";
	  }
	entry = vm_resolve(world.curjob->current, (elfsh_Addr) cur->entry, NULL);
	snprintf(logbuf, BUFSIZ, 
		 " Thread ID %10u %c %8s --[ started on %s from %s \n", 
		 (unsigned int) cur->tid, c, state, time, entry);
	fprintf(stderr, logbuf);
	//e2dbg_output(logbuf);
      }

  if (index == 0)
    e2dbg_output("\n [*] No threads in this process \n\n");
  else
    e2dbg_output("\n");
}


/* Switch on another thread and and Print all threads */
int		cmd_threads()
{
  e2dbgthread_t	*cur; 
  char		logbuf[BUFSIZ];

  PROFILER_IN(__FILE__, __FUNCTION__, __LINE__);

  /* Change current working thread if passed a parameter */
  if (world.curjob->curcmd->param[0])
    {

      //snprintf(logbuf, BUFSIZ, "%lu", strtoul(world.curjob->curcmd->param[0]));
      cur = hash_get(&e2dbgworld.threads, world.curjob->curcmd->param[0]);
      if (!cur)
	{
	  printf("Unknown thread -%s- \n", world.curjob->curcmd->param[0]);
	  PROFILER_ERR(__FILE__, __FUNCTION__, __LINE__, 
			    "Unknown thread", (-1));
	}
      
      /* Save modified registers in curthread, and load the new registers set */
      e2dbg_setregs();
      e2dbgworld.curthread = cur;
      e2dbg_getregs();
      snprintf(logbuf, BUFSIZ, " [*] Switched to thread %s \n\n", 
	       world.curjob->curcmd->param[0]);
      e2dbg_output(logbuf);
      PROFILER_ROUT(__FILE__, __FUNCTION__, __LINE__, 0);
    }

  /* Print all threads */
  e2dbg_threads_print();
  PROFILER_ROUT(__FILE__, __FUNCTION__, __LINE__, 0);
}




/* Stop all threads when a breakpoint happens */
int		e2dbg_thread_stopall(int signum)
{
  e2dbgthread_t	*cur;
  u_int         index;
  char		**keys;
  int		keynbr;
  char		*sig;
  int		total;
  int		ret;

#if __DEBUG_THREADS__
  u_int	called = rand() % 1000;

  /*
  printf(" [*] Stopping all user threads \n");
  printf("******** [%u] STOPALL (%u) threads [cur = %u [cnt = %u] dbg = %u ] ******* \n", 
	 called, e2dbgworld.threadnbr, 
	 (unsigned int) e2dbgworld.stoppedthread->tid, 
	 e2dbgworld.curthread->count, e2dbg_getid());
  */
#endif

  /* Iterate on the hash table of threads */
  sig = (signum == SIGUSR2 ? "SIGUSR2 to" : "Stopping");
  keys = hash_get_keys(&e2dbgworld.threads, &keynbr);
  for (total = index = 0; index < keynbr; index++)
    {
      cur = hash_get(&e2dbgworld.threads, keys[index]);

      /* Do not get the context of initializing threads */
      if (cur->state == E2DBG_THREAD_INIT)
	{
#if (__DEBUG_THREADS__ || __DEBUG_BP__)
	  fprintf(stderr, 
		  " [*] -NOT- %s thread ID %-10u (initializing) \n", 
		  sig, (unsigned int) cur->tid);
#endif
	  continue;
	}      

      if (cur->state == E2DBG_THREAD_BREAKING || cur->state == E2DBG_THREAD_STOPPING)
	{
#if (__DEBUG_THREADS__ || __DEBUG_BP__)
	  fprintf(stderr, 
		  " [*] -NOT- %s thread ID %-10u (already done or breaking) \n", 
		  sig, (unsigned int) cur->tid);
#endif
	  continue;
	}      
      
      if (cur->state == E2DBG_THREAD_SIGUSR2  || cur->state == E2DBG_THREAD_BREAKUSR2)
	{
#if (__DEBUG_THREADS__ || __DEBUG_BP__)
	  fprintf(stderr, 
		  " [*] -NOT- %s thread ID %-10u (inside sigusr2) \n", 
		  sig, (unsigned int) cur->tid);
#endif
	  continue;
	}      

      if (cur->initial)
	{
#if (__DEBUG_THREADS__ || __DEBUG_BP__)
	  fprintf(stderr, 
		 " [*] -NOT- %s thread ID %-10u (initial) \n", 
		  sig, (unsigned int) cur->tid);
#endif
	  continue;
	}      

      if (cur->tid == e2dbgworld.stoppedthread->tid)
	{
#if (__DEBUG_THREADS__ || __DEBUG_BP__)
	  fprintf(stderr,
		  " [*] -NOT- %s thread ID %-10u (current) \n", 
		  sig, (unsigned int) cur->tid);
#endif
	  continue;
	}      
      
#if (__DEBUG_THREADS__ || __DEBUG_BP__)
      fprintf(stderr, 
	      " [T] Sending %s to thread ID %-10u \n",
	      (signum == SIGUSR2 ? "SIGUSR2" : "SIGSTOP"), 
	      (unsigned int) cur->tid);
#endif
      
      /* If we send a SIGUSR2, increment the number of contexts to get */
      /* In case the thread is already stopped, send a CONT signal first */
      total++;
      if (signum == SIGUSR2)
	e2dbgworld.threadgotnbr++;
      else
	cur->state = E2DBG_THREAD_STOPPING;

      ret = e2dbg_kill((int) cur->tid, signum);
      if (ret)
	fprintf(stderr, "e2dbg_kill returned value %d \n", ret);
    }

  
#if __DEBUG_THREADS__
  //e2dbg_threads_print();
  //printf("--------- END OF STOPALL %u ------------ \n", called);
#endif

  return (total);
}





/* Continue all threads after a breakpoint */
void		e2dbg_thread_contall()
{
  e2dbgthread_t	*cur;
  char		**keys;
  int		keynbr;
  u_int         index;

#if (__DEBUG_THREADS__ || __DEBUG_BP__)
  fprintf(stderr, " [*] Continuing all threads (current number of threads = %u) \n",
	  e2dbgworld.threadnbr);
  e2dbg_threads_print();
#endif

  keys = hash_get_keys(&e2dbgworld.threads, &keynbr);

  /* Wake up all other threads after */
  for (index = 0; index < keynbr; index++)
    {
      cur = hash_get(&e2dbgworld.threads, keys[index]);

      /* Do not get the context of initializing threads */
      if (cur->state == E2DBG_THREAD_INIT)
	{
#if (__DEBUG_THREADS__ || __DEBUG_BP__)
	  fprintf(stderr,
		  " [*] -NOT- Continuing thread ID %-10u (initializing) \n", 
		  (unsigned int) cur->tid);
#endif
	  continue;
	}      

      if (cur->state == E2DBG_THREAD_SIGUSR2  || cur->state == E2DBG_THREAD_BREAKUSR2)
	{
#if (__DEBUG_THREADS__ || __DEBUG_BP__)
	  fprintf(stderr, 
		  " [*] -NOT- Continuing thread ID %-10u (inside sigusr2) \n", 
		  (unsigned int) cur->tid);
#endif
	  continue;
	}

      if (cur->state == E2DBG_THREAD_BREAKING)
	{
#if (__DEBUG_THREADS__ || __DEBUG_BP__)
	  fprintf(stderr, 
		  " [*] -NOT- Continuing thread ID %-10u (current) \n", 
		  (unsigned int) cur->tid);
#endif
	  continue;
	}      

      if (cur->initial)
	{
#if (__DEBUG_THREADS__ || __DEBUG_BP__)
	  fprintf(stderr,
		  " [*] -NOT- Continuing thread ID %-10u (initial) \n", 
		  (unsigned int) cur->tid);
#endif
	  continue;
	}      

      if (cur->tid == e2dbgworld.stoppedthread->tid)
	{
#if (__DEBUG_THREADS__ || __DEBUG_BP__)
	  fprintf(stderr,
		  " [*] -NOT- Continuing thread ID %-10u (stopping) \n", 
		  (unsigned int) cur->tid);
#endif
	  continue;
	}      

#if 1 //(__DEBUG_THREADS__ || __DEBUG_BP__)
      fprintf(stderr,
	      " [*] Continuing thread ID %-10u \n", 
	      (unsigned int) cur->tid);
#endif

      cur->state = E2DBG_THREAD_RUNNING;
      e2dbg_kill((int) cur->tid, SIGCONT);
    }

#if (__DEBUG_THREADS__ || __DEBUG_BP__)
  fprintf(stderr, " [*] End of thread continuations signal sending \n");
#endif

}
