## Process this file with automake to produce Makefile.in

## Created by Anjuta

AM_CPPFLAGS = \
	-DPACKAGE_LOCALE_DIR=\""$(prefix)/$(DATADIRNAME)/locale"\" \
	-DPACKAGE_SRC_DIR=\""$(srcdir)"\" \
	-DPACKAGE_DATA_DIR=\""$(datadir)"\" 

AM_CFLAGS =\
	 -Wall\
	 -g

bin_PROGRAMS = pthread_lib 

pthread_lib_SOURCES = \
	main.c       \
	ptl_linked_queue.c       \
	ptl_linked_queue.h       \
	ptl_queue.c       \
	ptl_queue.h       \
	ptl_util.h       \
	test/ptl_linked_queue_test.c       \
	ptl_linked_queue_test.h       \
	ptl_util.c       \
	ptl_signal_manager.c       \
	ptl_signal_manager.h       \
	ptl_thread_manager.c       \
	ptl_thread_manager.h       \
	ptl_thread_pool.h       \
	ptl_thread_pool.c       \
	ptl_array_list.c       \
	ptl_array_list.h       \
	ptl_task.c       \
	ptl_task.h       \
	ptl_array_queue.c       \
	ptl_array_queue.h       \
	ptl_header.h

pthread_lib_LDADD = \
	-lpthread

SUBDIRS = \
	test

