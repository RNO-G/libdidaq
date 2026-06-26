#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <sys/file.h>



// shortcut for cleanup attribute.
//

#define cleanup(X) __attribute__((__cleanup__(X)))

// helper macros so we can concat with line
#define CONCAT2(X,Y) X##Y
#define CONCAT(X,Y) CONCAT2(X,Y)


#define auto_free cleanup(__auto_free_fn)

static __attribute__((unused)) void __auto_free_fn(void * x)
{
  void ** X = (void **) x;
  free(*X);
}



# define lock_guard(X)  \
  pthread_mutex_t * CONCAT(__lockguard__,__LINE__) cleanup(__lock_guard_cleanup)= X; \
  pthread_mutex_lock(X);

//helper function for our lock_guard
static __attribute__((unused)) void __lock_guard_cleanup(pthread_mutex_t ** m)
{
  pthread_mutex_unlock(*m);
}



typedef int dummy_t __attribute__((unused));


#define flock_guard(_FD) \
 int  CONCAT(__flock_fd__,LINE) = cleanup( __flock_cleanup) = _FD;\
 flock(_FD,  LOCK_EX);

//helper function for our flock_guard
static __attribute__((unused)) void __flock_cleanup(int * fd)
{
  flock(*fd, LOCK_UN);
}



// defer the statement X until we exit scope
#define defer \
  auto void CONCAT(__defer__,__LINE__)(dummy_t*);\
  dummy_t CONCAT(__dummy__,__LINE__) cleanup(CONCAT(__defer__,__LINE__));\
  void CONCAT(__defer__,__LINE__)(dummy_t * __dummy__)

#define countof(X)  (sizeof(X)/sizeof(*X))
