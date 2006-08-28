/*
    pthread-lib is a set of pthread wrappers with additional features.
    Copyright (C) 2006  Nick Powers
    See <http://code.google.com/p/pthread-lib/> for more details and source.

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
*/

/*! @file multi_thread.c
    @brief A set of classes and wrappers to make using the pthread library easy
*/

#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <libgen.h>
#include "multi_thread.h"
#include "util.h"

/* Global Variables */
pthread_t *thread_pool = NULL;
int pool_size = 1;

/* Mutexes */
pthread_mutex_t thread_pool_mutex;
pthread_mutex_t pool_size_mutex;
pthread_mutex_t stop_mutex;
pthread_mutex_t status_array_mutex;

int *status_array = NULL; /* used by the thread manager */
int sizeof_status_array = 0;

/* Thread status */
int thread_hold;
int thread_stop = 0;

/* Constants */
const char *CLASS_NM = "multi_thread.c";

/* Private functions */
void  _lock_stop();
void  _unlock_stop();
void  _lock_status_array();
void  _unlock_status_array();
void  _lock_thread_pool();
void  _unlock_thread_pool();
void  _lock_pool_size();
void  _unlock_pool_size();

void  _create_local_mutexes();
void  _destroy_all_mutexes();
void *_signal_handler_function(void *functions);
int   _find_my_index(pthread_t thread);
void  _handle_ping_signal();

/*****************************************************************************/
/*! @fn void create_threads(void *(*func_ptr)(), void *parameter, int num_threads)
    @brief Creates \a num_threads threads executing \a func_ptr.

    @param void *(void *) func_ptr ptr to the function to be executed by each thread
    @param void *parameter generic parameter, each thread will have it's own local copy.
    @param int num_threads the number of threads to create

    Creates the threads and passes them the function to be executed and the
    generic parameter as a void ptr. The array of threads are stored in the
    thread_pool variable.
*/
/*****************************************************************************/
pthread_t *create_threads(void *(*func_ptr)(), void *parameter, int num_threads){
  int status;
  int thread_num;
  const char *METHOD_NM = "create_threads: ";

  pool_size = num_threads;

  /* Create memory */
  thread_pool = (pthread_t *)malloc(pool_size * sizeof(*thread_pool));

  if(thread_pool == NULL){
    LOG_ERROR(METHOD_NM,"Could not create threads!");
    exit(ERROR_CODE_MALLOC);
  }

  /* Initalize local mutex variables */
  _create_local_mutexes();

  /* Set the thread_stop flag to false */
  thread_stop = 0;

  /* Initalize all threads and pass the parameter */
  int i;
  for(i = 0; i < pool_size; i++){
    status = pthread_create(&thread_pool[i], NULL, func_ptr, parameter);
    CHECK_STATUS(status, "pthread_create", "thread bad status");
  }

  return thread_pool;
} /* end create_threads */


/******************************************************************************/
/******************************************************************************/
void _create_local_mutexes(){
  int status;
  status = pthread_mutex_init(&stop_mutex, NULL);
  CHECK_STATUS(status, "pthread_mutex_init", "stop_mutex bad status");

  status = pthread_mutex_init(&thread_pool_mutex, NULL);
  CHECK_STATUS(status, "pthread_mutex_init", "thread_pool_mutex bad status");

  status = pthread_mutex_init(&pool_size_mutex, NULL);
  CHECK_STATUS(status, "pthread_mutex_init", "pool_size_mutex bad status");

  status = pthread_mutex_init(&status_array_mutex, NULL);
  CHECK_STATUS(status, "pthread_mutex_init", "status_array_mutex bad status");
}

/******************************************************************************/
/* Calls pthread_join to syncronize and stop all thread workers. If the       */
/* return value is not equal the worker_num, it gives an error to stdout.     */
/* It also frees the thread_pool and parameter memory if allocated.             */
/* Lastly is destroys all mutexs defined in the create method.                */
/*                                                                            */
/* Parameters: int *: A set of integers that matches the number of threads    */
/*                    defined.  This should contain their status, 1 if ok,    */
/*                    0 if dead or lost.                                      */
/******************************************************************************/
void join_threads(int *t_status){
  int status = 0;
  void *ret_value = 0;
  int will_check_status = 1;
  const char *METHOD_NM = "join_threads: ";

  if(t_status == NULL){
    will_check_status = 0;
    printf("%s Joining on ALL threads reguardless of state \n", METHOD_NM);
  }

  int i;
  for(i = 0; i < pool_size; i++ ) {
    if(will_check_status){

      if(t_status[i]){

        status = pthread_join(thread_pool[i], &ret_value);
        CHECK_STATUS(status, "pthread_join", "bad status");

      } else {
        printf("%s Thread [%d] is hung, not joining\n",METHOD_NM, i + 1);
      }

    } else {
      printf("%s Going to stop thread [%d]\n",METHOD_NM, i + 1);
      status = pthread_join(thread_pool[i], &ret_value);
      CHECK_STATUS(status, "pthread_join", "bad status");
    }

    /* check return value? */
  }

  printf("%s Threads Stopped Successfully\n",METHOD_NM);

  FREE(thread_pool);

  _destroy_all_mutexes();

} /* end join_threads */

/*****************************************************************************/
/* Does a timed wait on the number of seconds passed in as a parameter.      */
/*                                                                           */
/* Parameters: int wait_secs - time, in seconds, to  wait before waking up   */
/*                             on it's own accord.                           */
/* Notes:                                                                    */
/* A local mutex and cond variable is used and destroyed.                    */
/*****************************************************************************/
int timed_wait(int wait_secs){
  int rc;
  struct timespec   ts;
  struct timeval    tp;
  const char *METHOD_NM = "timed_wait: ";

  memset(&tp,0,sizeof(tp));

  pthread_mutex_t timed_wait_mutex = PTHREAD_MUTEX_INITIALIZER;
  pthread_cond_t  timed_wait_cond = PTHREAD_COND_INITIALIZER;

  rc = gettimeofday(&tp, NULL);
  if(rc){
    LOG_ERROR(METHOD_NM, "gettimeofday failed in timed_wait");
    return rc;
  }

  /* Lock Mutex */
  int status = pthread_mutex_lock( &timed_wait_mutex);
  CHECK_STATUS(status, "pthread_mutex_lock", "bad status (timed_wait_mutex)");

  /* Get time in seconds */
  ts.tv_sec  = tp.tv_sec;
  ts.tv_nsec = tp.tv_usec * 1000;
  ts.tv_sec += wait_secs;

  /* Perform Wait */
  rc = pthread_cond_timedwait(&timed_wait_cond, &timed_wait_mutex, &ts);

  /* Unlock Mutex */
  status = pthread_mutex_unlock(&timed_wait_mutex);
  CHECK_STATUS(status, "pthread_mutex_unlock", "bad status (timed_wait_mutex)")

  /* Destroy local mutex and cond */
  status = pthread_mutex_destroy(&timed_wait_mutex);
  CHECK_STATUS(status,"pthread_mutex_destroy", "bad status (timed_wait_mutex)");

  status = pthread_cond_destroy(&timed_wait_cond);
  CHECK_STATUS(status,"pthread_cond_destroy", "bad status (timed_wait_cond)");

  return rc;
} /* end timed_wait */

/*****************************************************************************/
/* Does a timed wait on the number of milli seconds passed in as a parameter */
/*                                                                           */
/* Parameters: int wait_secs - time, in milliseconds, to  wait before waking */
/*                             up on it's own accord.                        */
/* Notes:                                                                    */
/* A local mutex and cond variable is used and destroyed.                    */
/*****************************************************************************/
int timed_wait_milli(int wait_secs){
  int rc;
  struct timespec   ts;
  struct timeval    tp;
  const char *METHOD_NM = "timed_wait_milli: ";

  if(wait_secs <= 0){
      return 0;
  }

  memset(&tp,0,sizeof(tp));
  memset(&ts,0,sizeof(ts));

  pthread_mutex_t timed_wait_mutex = PTHREAD_MUTEX_INITIALIZER;
  pthread_cond_t  timed_wait_cond = PTHREAD_COND_INITIALIZER;

  rc = gettimeofday(&tp, NULL);
  if(rc){
    LOG_ERROR(METHOD_NM, "gettimeofday failed");
    return rc;
  }

  /* Lock Mutex */
  int status = pthread_mutex_lock( &timed_wait_mutex);
  CHECK_STATUS(status, "pthread_mutex_lock", "bad status (timed_wait_mutex)");

  /* Get time in milliseconds */
  /* convert milli to nano */
  wait_secs *= 1000000;
  ts.tv_sec  = tp.tv_sec;
  ts.tv_nsec = tp.tv_usec + wait_secs;

  /* Perform Wait */
  rc = pthread_cond_timedwait(&timed_wait_cond, &timed_wait_mutex, &ts);

  /* Unlock Mutex */
  status = pthread_mutex_unlock(&timed_wait_mutex);
  CHECK_STATUS(status, "pthread_mutex_unlock", "bad status (timed_wait_mutex)");

  /* Destroy local mutex and cond */
  status = pthread_mutex_destroy(&timed_wait_mutex);
  CHECK_STATUS(status,"pthread_mutex_destroy", "bad status (timed_wait_mutex)");

  status = pthread_cond_destroy(&timed_wait_cond);
  CHECK_STATUS(status,"pthread_cond_destroy", "bad status (timed_wait_cond)");

  return rc;

}/* end timed_wait */


/*****************************************************************************/
/*! @fn int find_my_index(pthread_t thread)
    @brief Finds the index of this thread in \a thread_pool
    @param pthread_t thread Handle of this thread

    Parses through the thread_pool looking for itself to return the index
    of the thread. This is done so we may map it to the status array.

*/
/*****************************************************************************/
int _find_my_index(pthread_t thread){
  const char *METHOD_NM = "find_my_index: ";
  int i;
  int size = get_pool_size();

  _lock_thread_pool();
  for(i=0; i<size; i++){
    if(pthread_equal(thread, thread_pool[i])){
      break;
    }
  }
  _unlock_thread_pool();

  /* if we didn't find a thread, return a negative number */
  if(i >= size){
    i = -1;
    LOG_ERROR(METHOD_NM, "Unable to match any thread in thread_pool");
  }

  return i;
}

/* Mutex Operations */
/*************************************/
/*    Stop Working Operations        */
/*************************************/
void _lock_stop() {
  int status = pthread_mutex_lock(&stop_mutex);
  CHECK_STATUS(status, "pthread_mutex_lock", "bad status (stop_mutex)");
}

void _unlock_stop() {
  int status = pthread_mutex_unlock(&stop_mutex);
  CHECK_STATUS(status, "pthread_mutex_unlock", "bad status (stop_mutex)")
}

/*************************************/
/*   Status Array Operations         */
/*************************************/
void _lock_status_array(){
  int status = pthread_mutex_lock(&status_array_mutex);
  CHECK_STATUS(status, "pthread_mutex_lock", "bad status (status_array_mutex)");
}

void _unlock_status_array(){
  int status = pthread_mutex_unlock(&status_array_mutex);
  CHECK_STATUS(status, "pthread_mutex_lock", "bad status (status_array_mutex)");
}


/*************************************/
/*   Pool Size Mutex Operations      */
/*************************************/
void _lock_thread_pool(){
  int status = pthread_mutex_lock(&thread_pool_mutex);
  CHECK_STATUS(status, "pthread_mutex_lock", "bad status (thread_pool_mutex)");
}

void _unlock_thread_pool(){
  int status = pthread_mutex_unlock(&thread_pool_mutex);
  CHECK_STATUS(status, "pthread_mutex_lock", "bad status (thread_pool_mutex)");
}

/*************************************/
/*   Thread Pool Mutex Operations    */
/*************************************/
void _lock_pool_size(){
  int status = pthread_mutex_lock(&pool_size_mutex);
  CHECK_STATUS(status, "pthread_mutex_lock", "bad status (pool_size_mutex)");
}

void _unlock_pool_size(){
  int status = pthread_mutex_unlock(&pool_size_mutex);
  CHECK_STATUS(status, "pthread_mutex_lock", "bad status (pool_size_mutex)");
}

/******************************************************************************/
/*********************  Safe/Public Operations  *******************************/
/******************************************************************************/

/******************************************************************************/
/* Destroys all mutexes created in the create_threads() function              */
/*                                                                            */
/* Parameters: None                                                           */
/******************************************************************************/

void _destroy_all_mutexes(){
  int status;

  status = pthread_mutex_destroy(&stop_mutex);
  CHECK_STATUS(status,"pthread_mutex_destroy", "bad status (stop_mutex)");

  status = pthread_mutex_destroy(&thread_pool_mutex);
  CHECK_STATUS(status,"pthread_mutex_destroy", "bad status (thread_pool_mutex)");

  status = pthread_mutex_destroy(&pool_size_mutex);
  CHECK_STATUS(status,"pthread_mutex_destroy", "bad status (pool_size_mutex)");

  status = pthread_mutex_destroy(&status_array_mutex);
  CHECK_STATUS(status,"pthread_mutex_destroy", "bad status (status_array_mutex)");
}

/* Thread Operations */
/*************************************************/
/* Safe (Mutex stop_working is locked)           */
/* Sets worker_stop to TRUE.  This method should */
/*  be used to tell the workers to stop.         */
/*************************************************/
void stop_threads() {
  _lock_stop();
  thread_stop = 1;
  _unlock_stop();
}

/******************************************************************************/
/*! @fn int should_stop()
    @brief Returns the should_stop variable.

    This function can be used in a while loop to determine if the threads
    should stop.

    This function has a mutex and, therefore, is thread safe.
*/
/******************************************************************************/
int should_stop() {
  int should_stop = 0;

  _lock_stop();
  should_stop = thread_stop;
  _unlock_stop();

  return should_stop;
}

/******************************************************************************/
/*! @fun void set_stop(int stop)
    @brief Sets the thread_stop variable to the parameter \a stop.
    @param int stop flag to indicate whether or not to stop the threads.

    This function can be used to set the \a thread_stop variable for whatever
    purpose.

    This function has a mutex and, therefore, is thread safe.
*/
/******************************************************************************/
void set_stop(int stop){
  _lock_stop();
  thread_stop = stop;
  _unlock_stop();
}

/******************************************************************************/
/*! @fn int get_pool_size()
    @brief Returns the \a pool_size
*/
/******************************************************************************/
int get_pool_size(){
  int size = 0;
  _lock_pool_size();
  size = pool_size;
  _unlock_pool_size();

  return size;
}
/******************************************************************************/
/*! @fn int create_status_array()
    @brief Creates the array in which the threads update to indicate status

    This status is the pid of the thread.  If this status is unable to
    be updated, this means the thread is dead. If the malloc fails
    this thread will exit.

    This function has a mutex and, therefore, is thread safe.
*/
/******************************************************************************/
void create_status_array(){
  const char *METHOD_NM = "create_status_array: ";
  int rc = 0;

  _lock_status_array();

  FREE(status_array);
  sizeof_status_array = pool_size;
  status_array = (int *)malloc(sizeof(int)*sizeof_status_array);

  if(status_array == NULL){
    LOG_ERROR(METHOD_NM, "Unable to malloc memory for status_array");
    rc = ERROR_CODE_MALLOC;
  }

  _unlock_status_array();

  /* exit if error */
  if(rc){ exit(rc);}
}

/******************************************************************************/
/*!  @fn void init_status_array()
     @brief Initializes the \a status_array variable

     If the pool_size has changed, recreate the \a status_array. Otherwise,
     just reset all ints to 0. This function has a mutex lock and, therefore,
     is thread safe.
*/
/******************************************************************************/
void init_status_array(){
  /* if thread pool changed, recreate array */
  if(pool_size != sizeof_status_array){
    create_status_array();
  } else {
    /* otherwise, just set all elements to zero */
    _lock_status_array();

    memset(status_array, 0, sizeof(int)*sizeof_status_array);

    _unlock_status_array();
  }
}

/******************************************************************************/
/*! @fn int *get_status_array()
    @brief Returns the \a status_array variable

    This function has a mutex lock around it and, therefore, is thread safe.
*/
/******************************************************************************/
int *get_status_array(){
  int *sa = NULL;

  _lock_status_array();
  sa = status_array;
  _unlock_status_array();

  return sa;
}

/******************************************************************************/
/*! @fn BOOL set_status_element(int index, int status)
    @brief Sets a single int of the \a status_array variable
    @param int index the index of the element that will be set
    @param int status the status that will be set in the index-th element

    This function is used via each thread to report it's status.  Each thread
    should set the element to a unique number.

*/
/******************************************************************************/
BOOL set_status_element(int index, int status){
  BOOL rc = FALSE;

  _lock_status_array();

  if(index < sizeof_status_array){
    status_array[index] = status;
    rc = TRUE;
  }

  _unlock_status_array();

  return rc;
}

/******************************************************************************/
/********************  UnSafe/Private Operations  *****************************/
/******************************************************************************/
