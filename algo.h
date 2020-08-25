#ifndef __algo_h__
#define __algo_h__
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#ifdef __cplusplus
extern "C" {
#endif

#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/sendfile.h>
#include <dlfcn.h>

#include <unistd.h>
#include <sys/uio.h>
#include <sys/socket.h>
#include <sys/time.h> 
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <time.h> 
#include <math.h>
#include <stdbool.h>
#include <signal.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>
#include <linux/tcp.h> 

#define MEASURE_RELATIVE_DELAY 0 // this will change the content of the packet
#define SPROUT 0 

float a = 2.1;
float delayLimitDefault = 0.025;
float exponential_inc = 0.25;
float exponential_dec = 0.25;
int sleepCntMax = 8; 
float buffer_param = 1.1;
int g_isWireless = 0;
const char* delay_mark = "xxxxinfo";

#define LIMIT_SENT_AMOUNT 1 
int bufferDelay_avg_param = 8;

#define MAX_SENDER_FLOW 100
#define MAX_RECEIVER_FLOW 100

#define PRINT_TCP_INFO 1 

// for instant throughput
#define BYTES_RECORD_SEC_MAX 10
#define BYTES_RECORD_INTERVAL_MSEC 20 
#define BYTES_RECORD_MAX (BYTES_RECORD_SEC_MAX*1000/BYTES_RECORD_INTERVAL_MSEC)
#define THROUGHPUT_AVERAGE_INTERVAL 5

// for long-term throughput e.g., 1 sec interval
#define LT_BYTES_RECORD_MAX 600
#define LT_BYTES_RECORD_INTERVAL_MSEC 1000

typedef struct sendInfo 
{
	double bytes;
	double sendTime;
	int packetSeq;
	struct sendInfo * next;
} sendInfo;

typedef struct receiveInfo 
{
	long long unsigned bytes;
	double receiveTime; // at application layer
	struct receiveInfo * next;
} receiveInfo;

typedef struct senderFlowInfo
{
	int sockfd;
	float delayLimit; // in seconds
	float lastCheckTime; // in seconds
	float rtt_min; // in seconds
	int set_buf_size;
	pthread_mutex_t lock;
	FILE * pFile;
	FILE * pTcpInfoFile;
	float delayFlushTime;
	float tcpInfoFlushTime;
	sendInfo * front; // most recent one
	sendInfo * back; // most old one
	int check_stop;
	uint64_t seqnum;
	double estimatedSentBytesAtTcp;
	struct timeval startTime;
	float lastBufferDelay;
	float avgBufferDelay;
	float srtt;
	float sleepTime;
	float bytesRecords[BYTES_RECORD_MAX]; // tcp layer
	float lastRecTime;
	float ltBytesRecords[LT_BYTES_RECORD_MAX]; // app layer
	float averageThroughputAtTcp; // Kbps
	float rtt;
	int cwnd;
} senderFlowInfo;

typedef struct receiverFlowInfo
{
	int sockfd;
	pthread_mutex_t lock;
	FILE * pFile;
	FILE * pTcpInfoFile;
	float delayFlushTime;
	float tcpInfoFlushTime;
	receiveInfo * front; // most recent one
	receiveInfo * back; // most old one
	int check_stop;
	long long unsigned totalBytesAtTcp;
	long long unsigned totalBytesSum;
	struct timeval startTime;
	float avgBufferDelay;
	float bytesRecords[BYTES_RECORD_MAX]; // tcp layer
	float lastRecTime;
	float ltBytesRecords[LT_BYTES_RECORD_MAX]; // app layer
	int lastThroughputTime;
	float averageThroughputAtTcp; // Kbps 
	float rtt;
	int cwnd;
	float firstDiffTime;
} receiverFlowInfo;

typedef struct returnInfo
{
	ssize_t size;
	float bufferDelay;
	float throughputAtTcp;
	float rtt;
	int cwnd;
} returnInfo;

int find_fidx_sender( int sockfd );
int find_fidx_receiver( int sockfd );
void sleep( int sockfd, size_t len );

int memsearch( const char *hay, int haysize, const char *needle, int needlesize ); 
void write_time_in_packet( char *buf, size_t len );
float measure_sender( int sockfd, void* buf, size_t len );
void update_sender_seq( int sockfd, int size, struct returnInfo * ri );
returnInfo measure_receiver( int sockfd, void * buf, int send_size );

#ifdef __cplusplus
}; // end of extern "C"
#endif
#endif
