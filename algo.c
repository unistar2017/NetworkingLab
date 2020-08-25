#include "algo.h"

int fidx_sender = 0;
int fidx_receiver = 0;
senderFlowInfo flow_info_sender[MAX_SENDER_FLOW];
receiverFlowInfo flow_info_receiver[MAX_RECEIVER_FLOW];

inline int find_fidx_sender( int sockfd )
{
	int fidx = -1;
	
	pthread_mutex_lock(&lock_sender);
	for( int i=0; i<fidx_sender; i++ )
	{
		if( flow_info_sender[i].sockfd == sockfd )
		{
			fidx = i;
			break;
		}
	}
	pthread_mutex_unlock(&lock_sender);
	return fidx;
}

inline int find_fidx_receiver( int sockfd )
{
	int fidx = -1;
	
	pthread_mutex_lock(&lock_receiver);
	for( int i=0; i<fidx_receiver; i++ )
	{
		if( flow_info_receiver[i].sockfd == sockfd )
		{
			fidx = i;
			break;
		}
	}
	pthread_mutex_unlock(&lock_receiver);
	return fidx;
}

inline void sleep( int sockfd, size_t len )
{
  	if( fatal_error_in_progress == 1 ) return;

	int fidx = find_fidx_sender(sockfd);
	pthread_mutex_lock(&flow_info_sender[fidx].lock);
	int set_buf_size = flow_info_sender[fidx].set_buf_size;
	pthread_mutex_unlock(&flow_info_sender[fidx].lock);
	if( LIMIT_SENT_AMOUNT && set_buf_size>0 )
	{
		int sleepCnt = 0;
		while ( 1 )
		{
			pthread_mutex_lock(&flow_info_sender[fidx].lock);
			if( sleepCnt++ > sleepCntMax || flow_info_sender[fidx].seqnum + len - flow_info_sender[fidx].estimatedSentBytesAtTcp <= flow_info_sender[fidx].set_buf_size )
			{
				pthread_mutex_unlock(&flow_info_sender[fidx].lock);
				break;
			}
			pthread_mutex_unlock(&flow_info_sender[fidx].lock);
			usleep( 1000 * powf(sleepCnt,1.5) );
		}
	}
}

int memsearch( const char *hay, int haysize, const char *needle, int needlesize ) 
{
	int haypos, needlepos;
	haysize -= needlesize;
	for (haypos = 0; haypos <= haysize; haypos++) {
		for (needlepos = 0; needlepos < needlesize; needlepos++) {
			if (hay[haypos + needlepos] != needle[needlepos]) {
				break;
			}
		}
		if (needlepos == needlesize) {
			return haypos;
		}
	}
	return -1;
}

void inline write_time_in_packet( char *buf, size_t len )
{
	int i=0;
	int write_size = 24;
	int zero_num = 0;
	struct timeval tv;
	
	if(DEBUG) print_packet( (char*) buf, len );
	if( !MEASURE_RELATIVE_DELAY || buf==NULL || len<write_size ) return;
	for( ; i<len-write_size; i++ )
	{
		if( !SPROUT && ( buf[i]==0 || buf[i]==' ' ) || SPROUT && buf[i]=='x' ) // 'x' is for sprout
			zero_num++;
		else
			zero_num = 0;
		if( zero_num == write_size )
		{
			gettimeofday( &tv, NULL );
			strncpy( &buf[i-write_size+1], delay_mark, 8 );  
			*((time_t*)(&buf[i-write_size+9])) = tv.tv_sec; 
			*((suseconds_t*)(&buf[i-write_size+17])) = tv.tv_usec; 
			if(DEBUG) print_packet( (char*) buf, len );
			return;
		}
	}
}

inline float measure_sender( int sockfd, void *buf, size_t len )
{
  	if( fatal_error_in_progress == 1 ) return 0;

	struct timeval tv;
	int32_t e=0;
	int fidx = find_fidx_sender(sockfd);
	double elapseTime = 0;
	float bufferDelay = 0;

	gettimeofday( &tv, NULL );

	pthread_mutex_lock(&flow_info_sender[fidx].lock);
	elapseTime = (tv.tv_sec-flow_info_sender[fidx].startTime.tv_sec)+((double)tv.tv_usec-flow_info_sender[fidx].startTime.tv_usec)*0.000001;
	if( flow_info_sender[fidx].front==NULL )
	{
		flow_info_sender[fidx].front = (sendInfo *) malloc(sizeof(sendInfo));
	}						
	else
	{
		sendInfo * oldFront = flow_info_sender[fidx].front;
		flow_info_sender[fidx].front = (sendInfo *) malloc(sizeof(sendInfo));
		oldFront->next = flow_info_sender[fidx].front;
	}
	flow_info_sender[fidx].front->sendTime = elapseTime;
	flow_info_sender[fidx].front->bytes = flow_info_sender[fidx].seqnum;		
	flow_info_sender[fidx].front->packetSeq += 1;
	flow_info_sender[fidx].front->next = NULL;
	if( flow_info_sender[fidx].back==NULL )
		flow_info_sender[fidx].back = flow_info_sender[fidx].front;
	bufferDelay = flow_info_sender[fidx].lastBufferDelay;
	if( flow_info_sender[fidx].sleepTime > 0 )
		usleep( flow_info_sender[fidx].sleepTime );
	write_time_in_packet( (char *)buf, len );
	pthread_mutex_unlock(&flow_info_sender[fidx].lock);
	return bufferDelay;
}

inline void update_sender_seq( int sockfd, int size, struct returnInfo * ri )
{
  	if( fatal_error_in_progress == 1 ) return;

	struct timeval tv;
	double elapseTime = 0;
	int fidx = find_fidx_sender(sockfd);
	pthread_mutex_lock(&flow_info_sender[fidx].lock);
	gettimeofday( &tv, NULL );
	elapseTime = (tv.tv_sec-flow_info_sender[fidx].startTime.tv_sec)+((double)tv.tv_usec-flow_info_sender[fidx].startTime.tv_usec)*0.000001;
	flow_info_sender[fidx].seqnum += size;
	flow_info_sender[fidx].ltBytesRecords[(int)elapseTime] += size;
	ri->throughputAtTcp = flow_info_sender[fidx].averageThroughputAtTcp;
	ri->rtt = flow_info_sender[fidx].rtt;
	ri->cwnd = flow_info_sender[fidx].cwnd;
	pthread_mutex_unlock(&flow_info_sender[fidx].lock);
}

inline returnInfo measure_receiver( int sockfd, void * buf, int recv_size )
{
	returnInfo ret;
  	if( fatal_error_in_progress == 1 ) return ret;

	int n = 0;
	int fidx = find_fidx_receiver(sockfd);
	float bufferDelay = 0;
	struct timeval tv;
	double currentTime = 0;
	double elapseTime = 0;
	double relativeDelay = 0;
	int recentTimeForGoodput = -1;
	double recentGoodput = 0;
	
	gettimeofday( &tv, NULL );
	currentTime = (double) tv.tv_sec + ((double) tv.tv_usec)/1000000;
	
	pthread_mutex_lock(&flow_info_receiver[fidx].lock);
	elapseTime = (tv.tv_sec-flow_info_receiver[fidx].startTime.tv_sec)+((double)tv.tv_usec-flow_info_receiver[fidx].startTime.tv_usec)*0.000001;
	flow_info_receiver[fidx].ltBytesRecords[((int)elapseTime)] += recv_size;
	flow_info_receiver[fidx].totalBytesSum += recv_size;
	while( 1 )
	{
		if( flow_info_receiver[fidx].back )
		{
			if( flow_info_receiver[fidx].back->bytes<=flow_info_receiver[fidx].totalBytesSum )
			{
				receiveInfo * oldBack = flow_info_receiver[fidx].back;
				flow_info_receiver[fidx].back = flow_info_receiver[fidx].back->next;
				free(oldBack);
				if(flow_info_receiver[fidx].back == NULL )
					flow_info_receiver[fidx].front = NULL;
			}
			else
			{

				bufferDelay = elapseTime - flow_info_receiver[fidx].back->receiveTime;
				break;
			}
		}
		else
			break;
	}
	if(DEBUG) print_packet( (char*) buf, recv_size );
	if( MEASURE_RELATIVE_DELAY && buf!=NULL )
	{
		char * pBuf = (char *) buf;
		int pos = memsearch( pBuf, recv_size, delay_mark, strlen(delay_mark) );
		if( DEBUG ) printf( "pos: %d recv_size %d \n", pos, recv_size );
		if( pos>=0 && pos+24<=recv_size )
		{
			double senderTime = ((double)(*((time_t*)(pBuf+pos+8)))) + (((double)(*((suseconds_t*)(pBuf+pos+16))))/1000000);
			if( flow_info_receiver[fidx].firstDiffTime==0 )
				flow_info_receiver[fidx].firstDiffTime = currentTime - senderTime;
			relativeDelay = (currentTime - senderTime) - flow_info_receiver[fidx].firstDiffTime;
			// for sprout : fill 'x'
			if( SPROUT )
			{
				for( int i=0; i<24; i++ )
				{
					*(pBuf+pos+i) = 'x';
				}	
			}
		}
	}

	if(DEBUG) print_packet( (char *)buf, recv_size );
	if( elapseTime>=1 )
	{
		recentTimeForGoodput = (int)(elapseTime - 1);
		if( flow_info_receiver[fidx].lastThroughputTime<recentTimeForGoodput )
		{
			recentGoodput = flow_info_receiver[fidx].ltBytesRecords[recentTimeForGoodput]*8/1024;
			flow_info_receiver[fidx].lastThroughputTime = recentTimeForGoodput;
		}
		else
			recentTimeForGoodput = -1;
	}
	if( flow_info_receiver[fidx].pFile )
	{
		fprintf(flow_info_receiver[fidx].pFile, "%f %f %d %f %d %f\n", elapseTime, bufferDelay, recv_size, relativeDelay, recentTimeForGoodput, recentGoodput );				
		if( flow_info_receiver[fidx].delayFlushTime==0 || 
		    elapseTime - flow_info_receiver[fidx].delayFlushTime > OUTPUT_FLUSH_INTERVAL )
		{
			fflush(flow_info_receiver[fidx].pFile);
			flow_info_receiver[fidx].delayFlushTime = elapseTime;
		}
	}
	if( flow_info_receiver[fidx].avgBufferDelay == 0 )
		flow_info_receiver[fidx].avgBufferDelay = bufferDelay;
	else
		flow_info_receiver[fidx].avgBufferDelay = 
			flow_info_receiver[fidx].avgBufferDelay*7/8 +
			bufferDelay/8;

	pthread_mutex_unlock(&flow_info_receiver[fidx].lock);
	ret.size = recv_size;
	ret.bufferDelay = bufferDelay;
	ret.throughputAtTcp = flow_info_receiver[fidx].averageThroughputAtTcp;
	ret.rtt = flow_info_receiver[fidx].rtt;
	ret.cwnd = flow_info_receiver[fidx].cwnd;
	return ret;
}
