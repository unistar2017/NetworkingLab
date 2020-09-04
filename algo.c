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

void * tcp_info_check_thread_for_sender(void* tmp)
{ 
	int check_interval = CHECK_INTERVAL * 1000;
	
	pthread_mutex_lock(&lock_sender);
	for( int fidx=0; fidx<fidx_sender; fidx++ )
	{
		pthread_mutex_lock(&flow_info_sender[fidx].lock);
		if( flow_info_sender[fidx].pTcpInfoFile!=NULL ) 
       			fprintf(flow_info_sender[fidx].pTcpInfoFile, "time\tsnd_mss\tlast_data_sent\tlast_data_recv\tsnd_cwnd\tsnd_ssthresh\trcv_ssthresh\trtt\trtt_var\tunacked\tsacked\tlost\tretrans\tfackets\ttotal_retrans\tbytes_acked\tbytes_received\tsegs_out\tsegs_in\n");
		pthread_mutex_unlock(&flow_info_sender[fidx].lock);	
	}
	pthread_mutex_unlock(&lock_sender);

	while(1)
	{	
		struct timeval tv_start;
		gettimeofday( &tv_start, NULL );

		for( int fidx=0; fidx<fidx_sender; fidx++ )
		{
			pthread_mutex_lock(&flow_info_sender[fidx].lock);
			if( flow_info_sender[fidx].check_stop==1 )
				continue;

			int sockfd = flow_info_sender[fidx].sockfd;
			struct tcp_info ti;
			socklen_t tcp_info_length = sizeof(struct tcp_info);
			struct timeval tv;
			gettimeofday( &tv, NULL );
			double elapseTime = (tv.tv_sec-flow_info_sender[fidx].startTime.tv_sec)+((double)tv.tv_usec-flow_info_sender[fidx].startTime.tv_usec)*0.000001;
			int rc = getsockopt( sockfd, 6, TCP_INFO, (void *)&ti, &tcp_info_length );
			if( rc==0 )
			{ 

				if( flow_info_sender[fidx].pTcpInfoFile!=NULL ) 
				{
					fprintf(flow_info_sender[fidx].pTcpInfoFile, "%.6f\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%llu\t%llu\t%u\t%u\n",
							elapseTime,
							ti.tcpi_snd_mss,
							ti.tcpi_last_data_sent,
							ti.tcpi_last_data_recv,
							ti.tcpi_snd_cwnd,
							ti.tcpi_snd_ssthresh,
							ti.tcpi_rcv_ssthresh,
							ti.tcpi_rtt,
							ti.tcpi_rttvar,
							ti.tcpi_unacked,
							ti.tcpi_sacked,
							ti.tcpi_lost,
							ti.tcpi_retrans,
							ti.tcpi_fackets,
							ti.tcpi_total_retrans,
							ti.tcpi_bytes_acked,
							ti.tcpi_bytes_received,
							ti.tcpi_segs_out,
							ti.tcpi_segs_in
					       );
					if( flow_info_sender[fidx].tcpInfoFlushTime==0 || 
					    elapseTime - flow_info_sender[fidx].tcpInfoFlushTime > OUTPUT_FLUSH_INTERVAL )
					{
						fflush(flow_info_sender[fidx].pTcpInfoFile);
						flow_info_sender[fidx].tcpInfoFlushTime = elapseTime;
					}
				}
			}
			double estimatedSentBytes = ti.tcpi_bytes_acked + ((double)ti.tcpi_unacked) * ti.tcpi_snd_mss; 
			double recentBytes = estimatedSentBytes - flow_info_sender[fidx].estimatedSentBytesAtTcp;	
			int recIdx = ((int) (elapseTime*1000/BYTES_RECORD_INTERVAL_MSEC)) % BYTES_RECORD_MAX;
			int lastIdx = ((int) (flow_info_sender[fidx].lastRecTime*1000/BYTES_RECORD_INTERVAL_MSEC)) % BYTES_RECORD_MAX;
			double bytesSum = 0;
			if( lastIdx!=recIdx )
				flow_info_sender[fidx].bytesRecords[recIdx] = recentBytes;
			else
				flow_info_sender[fidx].bytesRecords[recIdx] += recentBytes;
			for( int i=0; i<THROUGHPUT_AVERAGE_INTERVAL; i++ )
			{
				int curIdx = recIdx - i;
				if( curIdx<0 )
					curIdx += BYTES_RECORD_MAX;
				bytesSum += flow_info_sender[fidx].bytesRecords[curIdx];
			}
			flow_info_sender[fidx].averageThroughputAtTcp = bytesSum*8/((float)THROUGHPUT_AVERAGE_INTERVAL*BYTES_RECORD_INTERVAL_MSEC/1000)/1024;	
			flow_info_sender[fidx].lastRecTime = elapseTime;

			flow_info_sender[fidx].estimatedSentBytesAtTcp = estimatedSentBytes;
			while( 1 )
			{
				if( flow_info_sender[fidx].back!=NULL && flow_info_sender[fidx].back->bytes<=estimatedSentBytes )
				{
					double bufferDelay = elapseTime - flow_info_sender[fidx].back->sendTime;
					flow_info_sender[fidx].lastBufferDelay = bufferDelay;
					if( flow_info_sender[fidx].avgBufferDelay == 0 )
						flow_info_sender[fidx].avgBufferDelay = bufferDelay;
					else
						flow_info_sender[fidx].avgBufferDelay = 
							flow_info_sender[fidx].avgBufferDelay*(bufferDelay_avg_param-1)/bufferDelay_avg_param +
							bufferDelay/bufferDelay_avg_param;
					sendInfo * oldBack = flow_info_sender[fidx].back;
					struct timespec tp;
					clock_gettime(CLOCK_MONOTONIC, &tp);
					if( flow_info_sender[fidx].pFile )
					{
						fprintf(flow_info_sender[fidx].pFile, "%f %f %u %u %u %ld.%09ld\n", elapseTime, bufferDelay, ti.tcpi_snd_cwnd, ti.tcpi_snd_ssthresh, ti.tcpi_rtt, tp.tv_sec, tp.tv_nsec );
						if( flow_info_sender[fidx].delayFlushTime==0 || 
						    elapseTime - flow_info_sender[fidx].delayFlushTime > OUTPUT_FLUSH_INTERVAL )
						{
							fflush(flow_info_sender[fidx].pFile);
							flow_info_sender[fidx].delayFlushTime = elapseTime;
						}
					}
					//fprintf(flow_info_sender[fidx].pFile, "%f %f %u %u %u\n", elapseTime, bufferDelay, ti.tcpi_snd_cwnd, ti.tcpi_snd_ssthresh, ti.tcpi_rtt );
					flow_info_sender[fidx].back = flow_info_sender[fidx].back->next;
					if(flow_info_sender[fidx].back == NULL )
						flow_info_sender[fidx].front = NULL;

					free(oldBack);
				}
				else
					break;
			}
			float rtt = ((float) ti.tcpi_rtt)/1000000; 
			float srtt = rtt;

			if( flow_info_sender[fidx].srtt == 0 )
				flow_info_sender[fidx].srtt = rtt;
			else
				flow_info_sender[fidx].srtt = flow_info_sender[fidx].srtt*7/8 + rtt/8;
			srtt = flow_info_sender[fidx].srtt;			 

			flow_info_sender[fidx].rtt = rtt; 
			flow_info_sender[fidx].cwnd = ti.tcpi_snd_cwnd;

			if( control_alg == ALG_LIMIT_BASED && (elapseTime - flow_info_sender[fidx].lastCheckTime > srtt))
			{
				if( flow_info_sender[fidx].rtt_min==0 || flow_info_sender[fidx].rtt_min > srtt )
					flow_info_sender[fidx].rtt_min = srtt;
				int sockbufsize=0; 
				unsigned int size=sizeof(unsigned int); 
				int err = 0;
				if( flow_info_sender[fidx].set_buf_size==0 )
					err = getsockopt( flow_info_sender[fidx].sockfd, SOL_SOCKET, SO_SNDBUF, &sockbufsize, &size );
				else
					sockbufsize = flow_info_sender[fidx].set_buf_size;
				int setsize = 0;
				int setsize_real = 0;
				int type = 0;
				float ratio = ( flow_info_sender[fidx].avgBufferDelay ) / flow_info_sender[fidx].delayLimit;
				float powratio = 0;
				if( ratio > 1 )
				{
					powratio = powf(ratio,exponential_dec);
					setsize = (int) ((float)sockbufsize / powratio);
					type = 3; 
				}
				else if( ratio < 1 )
				{
					powratio = powf(ratio,exponential_inc);
					setsize = (int) ((float)sockbufsize / powratio);
					type = 1;
				}
				else 
				{
					setsize = sockbufsize;
					type = 2;
				}

				flow_info_sender[fidx].lastCheckTime = elapseTime; 
				if( setsize>(float)ti.tcpi_snd_cwnd * ti.tcpi_snd_mss * a )
				{
					setsize = ti.tcpi_snd_cwnd * ti.tcpi_snd_mss * a;
					type = 4; 
					flow_info_sender[fidx].lastCheckTime = elapseTime; 
				}
				if( setsize < 4096 )
					setsize = 4096;
				if( setsize>0 )
				{
					if( g_isWireless )
					{
						setsize_real = setsize/2*buffer_param;
						if (setsockopt( flow_info_sender[fidx].sockfd, SOL_SOCKET, SO_SNDBUF, &setsize_real, sizeof(int)) == -1) {
							if(DEBUG) printf("Error setting socket opts: %s\n", strerror(errno));
						}
						else
						{
							if(DEBUG) printf("Send buffer is changed to %d from %d. \n", setsize, sockbufsize );
						}
					}
					flow_info_sender[fidx].set_buf_size = setsize;
				}
				if(DEBUG) printf( "rtt %f srtt %f bufferDelay %f rtt_min %f setsize %d type %d \n", rtt, srtt, flow_info_sender[fidx].avgBufferDelay, flow_info_sender[fidx].rtt_min, setsize, type );
			}

			pthread_mutex_unlock(&flow_info_sender[fidx].lock);
		}
		struct timeval tv_end;
		gettimeofday( &tv_end, NULL );
		check_interval = CHECK_INTERVAL * 1000 - (1000000 * ( tv_end.tv_sec - tv_start.tv_sec ) + tv_end.tv_usec - tv_start.tv_usec );
		if(check_interval<0) check_interval = 0;
		usleep(check_interval);	
		if( global_check_thread_stop==1 )
		{
			break;
		}
	}
}	

void * tcp_info_check_thread_for_receiver(void* tmp)
{ 
	int check_interval = CHECK_INTERVAL * 1000;

	pthread_mutex_lock(&lock_receiver);
	for( int fidx=0; fidx<fidx_receiver; fidx++ )
	{
		pthread_mutex_lock(&flow_info_receiver[fidx].lock);
		if( flow_info_receiver[fidx].pTcpInfoFile!=NULL ) 
        		fprintf(flow_info_receiver[fidx].pTcpInfoFile, "time\tsnd_mss\tlast_data_sent\tlast_data_recv\tsnd_cwnd\tsnd_ssthresh\trcv_ssthresh\trtt\trtt_var\tunacked\tsacked\tlost\tretrans\tfackets\ttotal_retrans\tbytes_acked\tbytes_received\tsegs_out\tsegs_in\n");
		pthread_mutex_unlock(&flow_info_receiver[fidx].lock);
	}
	pthread_mutex_unlock(&lock_receiver);

	while( 1 )
	{	
		struct timeval tv_start;
		gettimeofday( &tv_start, NULL );

		for( int fidx=0; fidx<fidx_receiver; fidx++ )
		{
			pthread_mutex_lock(&flow_info_receiver[fidx].lock);
			if( flow_info_receiver[fidx].check_stop==1 )
				continue;

			int sockfd = flow_info_receiver[fidx].sockfd;
			struct tcp_info ti;
			long long unsigned totalBytesRecent = 0;
			socklen_t tcp_info_length = sizeof(struct tcp_info);
			struct timeval tv;
			gettimeofday( &tv, NULL );
			double currentTime = (tv.tv_sec-flow_info_receiver[fidx].startTime.tv_sec)+((double)tv.tv_usec-flow_info_receiver[fidx].startTime.tv_usec)*0.000001;
			if ( getsockopt( sockfd, 6, TCP_INFO, (void *)&ti, &tcp_info_length ) == 0 ) {
				totalBytesRecent = ti.tcpi_segs_in * ti.tcpi_rcv_mss; 
				if( flow_info_receiver[fidx].pTcpInfoFile!=NULL ) 
				{
					fprintf( flow_info_receiver[fidx].pTcpInfoFile, "%.6f\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%llu\t%llu\t%u\t%u\n",
							currentTime,
							ti.tcpi_snd_mss,
							ti.tcpi_last_data_sent,
							ti.tcpi_last_data_recv,
							ti.tcpi_snd_cwnd,
							ti.tcpi_snd_ssthresh,
							ti.tcpi_rcv_ssthresh,
							ti.tcpi_rtt,
							ti.tcpi_rttvar,
							ti.tcpi_unacked,
							ti.tcpi_sacked,
							ti.tcpi_lost,
							ti.tcpi_retrans,
							ti.tcpi_fackets,
							ti.tcpi_total_retrans,
							ti.tcpi_bytes_acked,
							ti.tcpi_bytes_received,
							ti.tcpi_segs_out,
							ti.tcpi_segs_in
					       );
					if( flow_info_receiver[fidx].tcpInfoFlushTime==0 || 
					    currentTime - flow_info_receiver[fidx].tcpInfoFlushTime > OUTPUT_FLUSH_INTERVAL )
					{
						fflush(flow_info_receiver[fidx].pTcpInfoFile);
						flow_info_receiver[fidx].tcpInfoFlushTime = currentTime;
					}
				}
			}
			flow_info_receiver[fidx].rtt = ti.tcpi_rtt; 
			flow_info_receiver[fidx].cwnd = ti.tcpi_snd_cwnd;

			if( totalBytesRecent>flow_info_receiver[fidx].totalBytesAtTcp )
			{
				double recentBytes = totalBytesRecent - flow_info_receiver[fidx].totalBytesAtTcp;	
				int recIdx = ((int) (currentTime*1000/BYTES_RECORD_INTERVAL_MSEC)) % BYTES_RECORD_MAX;
				int lastIdx = ((int) (flow_info_receiver[fidx].lastRecTime*1000/BYTES_RECORD_INTERVAL_MSEC)) % BYTES_RECORD_MAX;
				double bytesSum = 0;
				if( lastIdx!=recIdx )
					flow_info_receiver[fidx].bytesRecords[recIdx] = recentBytes;
				else
					flow_info_receiver[fidx].bytesRecords[recIdx] += recentBytes;
				for( int i=0; i<THROUGHPUT_AVERAGE_INTERVAL; i++ )
				{
					int curIdx = recIdx - i;
					if( curIdx<0 )
						curIdx += BYTES_RECORD_MAX;
					bytesSum += flow_info_receiver[fidx].bytesRecords[curIdx];
				}
				flow_info_receiver[fidx].averageThroughputAtTcp = bytesSum*8/((float)THROUGHPUT_AVERAGE_INTERVAL*BYTES_RECORD_INTERVAL_MSEC/1000)/1024; 		
				flow_info_receiver[fidx].lastRecTime = currentTime;
				flow_info_receiver[fidx].totalBytesAtTcp = totalBytesRecent;

				if( flow_info_receiver[fidx].front==NULL )
				{
					flow_info_receiver[fidx].front = (receiveInfo *) malloc(sizeof(receiveInfo));
				}						
				else
				{
					receiveInfo * oldFront = flow_info_receiver[fidx].front;
					flow_info_receiver[fidx].front = (receiveInfo *) malloc(sizeof(receiveInfo));
					oldFront->next = flow_info_receiver[fidx].front;
				}
				flow_info_receiver[fidx].front->bytes = totalBytesRecent;		
				flow_info_receiver[fidx].front->receiveTime = currentTime;
				flow_info_receiver[fidx].front->next = NULL;
				if( flow_info_receiver[fidx].back==NULL )
					flow_info_receiver[fidx].back = flow_info_receiver[fidx].front;
			}
			pthread_mutex_unlock(&flow_info_receiver[fidx].lock);
		}
		struct timeval tv_end;
		gettimeofday( &tv_end, NULL );
		check_interval = CHECK_INTERVAL * 1000 - (1000000 * ( tv_end.tv_sec - tv_start.tv_sec ) + tv_end.tv_usec - tv_start.tv_usec );
		if(check_interval<0) check_interval = 0;
		usleep(check_interval);	
		if( global_check_thread_stop==1 )
		{
			break;
		}
	}
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
