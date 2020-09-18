#include "algo.h"

pthread_mutex_t lock_sender;
pthread_mutex_t lock_receiver;
int fidx_sender = 0;
int fidx_receiver = 0;
senderFlowInfo flow_info_sender[MAX_SENDER_FLOW];
receiverFlowInfo flow_info_receiver[MAX_RECEIVER_FLOW];
pthread_t sender_check_thread;
pthread_t receiver_check_thread;
int global_check_thread_stop = 0;

int control_alg = ALG_NONE;

void print_packet( char * buf, int len )
{
	for( int i=0; i<len; i++ )
	{
		printf( "%d ", (int)(*(buf+i)) );
		if(i%100==99) printf("\n");
	}
	printf("\n");
}

void print_sender_list(int fidx)
{
	sendInfo * p = flow_info_sender[fidx].back;
	while( p )
	{
		printf( "%lx ", (long unsigned int)p );
		p = p->next;
	}
	printf( "\n" );
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

int initialize_algo( bool isWireless, int algorithm )
{
	int rc = pthread_mutex_init(&lock_sender, NULL);
	if(rc)
	{
	    	printf("sender flow information list lock init failed");
		return rc;
  	}
	rc = pthread_mutex_init(&lock_receiver, NULL);
	if(rc)
	{
	    	printf("receiver flow information list lock init failed");
		return rc;
  	}
	g_isWireless = isWireless;
	control_alg = algorithm;
	
#ifdef LD_PRELOAD
	if (!original_send) {
	    original_send = dlsym(RTLD_NEXT, "send");
	}
	if (!original_sendto) {
	    original_sendto = dlsym(RTLD_NEXT, "sendto");
	}
	if (!original_sendmsg) {
	    original_sendmsg = dlsym(RTLD_NEXT, "sendmsg");
	}
	if (!original_write) {
	    original_write = dlsym(RTLD_NEXT, "write");
	}
	if (!original_recv) {
	    original_recv = dlsym(RTLD_NEXT, "recv");
	}
	if (!original_recvfrom) {
	    original_recvfrom = dlsym(RTLD_NEXT, "recvfrom");
	}
	if (!original_recvmsg) {
	    original_recvmsg = dlsym(RTLD_NEXT, "recvmsg");
	}
	if (!original_read) {
	    original_read = dlsym(RTLD_NEXT, "read");
	}
	if (!original_writev) {
	    original_writev = dlsym(RTLD_NEXT, "writev");
	}
	if (!original_readv) {
	    original_readv = dlsym(RTLD_NEXT, "readv");
	}
	if (!original_sendfile) {
	    original_sendfile = dlsym(RTLD_NEXT, "sendfile");
	}
#endif
	rc = pthread_create(&sender_check_thread, NULL, tcp_info_check_thread_for_sender, NULL);
	if (rc) {
		printf("failed to create tcp_info_check_thread_for_sender. return code : %d",rc);
		return rc;
	}	
	rc = pthread_create(&receiver_check_thread, NULL, tcp_info_check_thread_for_receiver, NULL);
	if (rc) {
		printf("failed to create tcp_info_check_thread_for_receiver. return code : %d",rc);
		return rc;
	}	
}

int initialize_algo_flow_for_sender( int sockfd, char * sender_delay_filename, char * tcpinfo_filename )
{
	pthread_mutex_lock(&lock_sender);
	memset( &flow_info_sender[fidx_sender], 0, sizeof(senderFlowInfo) );
	int rc = pthread_mutex_init(&flow_info_sender[fidx_sender].lock, NULL);
	if(rc)
	{
	    	printf("flow information lock init failed");
		pthread_mutex_unlock(&lock_sender);
		return rc;
  	}
	
	flow_info_sender[fidx_sender].sockfd = sockfd;
	flow_info_sender[fidx_sender].lastCheckTime = 0;
	flow_info_sender[fidx_sender].rtt_min = 0;
	flow_info_sender[fidx_sender].set_buf_size = 0;
	flow_info_sender[fidx_sender].delayLimit = delayLimitDefault;
	
	if( sender_delay_filename!=NULL )
		flow_info_sender[fidx_sender].pFile = fopen (sender_delay_filename,"w+");
	if( tcpinfo_filename!=NULL ) 
                flow_info_sender[fidx_sender].pTcpInfoFile = fopen (tcpinfo_filename,"w+");
	gettimeofday( &flow_info_sender[fidx_sender].startTime, NULL );

	fidx_sender++;
	pthread_mutex_unlock(&lock_sender);
	if( DEBUG ) printf( "initializing sender flow %d\n", sockfd );
	return rc;
}

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

returnInfo send_algo( int sockfd, const void *buf, size_t len, int flags )
{
	returnInfo ret;
	ret.bufferDelay = measure_sender( sockfd, (char*) buf, len );
#ifdef LD_PRELOAD
	ret.size = original_send(sockfd, buf, len, flags); 
#else
	ret.size = send(sockfd, buf, len, flags); 
#endif
	update_sender_seq( sockfd, ret.size, &ret );
	sleep_after_sending( sockfd, ret.size );
	return ret;
}

returnInfo sendto_algo(int sockfd, const void *buf, size_t len, int flags,
                      const struct sockaddr *dest_addr, socklen_t addrlen)
{
	returnInfo ret;
	ret.bufferDelay = measure_sender( sockfd, (char*) buf, len );
#ifdef LD_PRELOAD
	ret.size = original_sendto(sockfd, buf, len, flags, dest_addr, addrlen); 
#else
	ret.size = sendto(sockfd, buf, len, flags, dest_addr, addrlen); 
#endif
	update_sender_seq( sockfd, ret.size, &ret );
	sleep_after_sending( sockfd, ret.size );
	return ret;
}

returnInfo sendmsg_algo(int sockfd, const struct msghdr *msg, int flags)
{
	returnInfo ret;
	ret.bufferDelay = measure_sender( sockfd, (char*) NULL, 0 );
#ifdef LD_PRELOAD
	ret.size = original_sendmsg(sockfd, msg, flags); 
#else
	ret.size = sendmsg(sockfd, msg, flags); 
#endif
	update_sender_seq( sockfd, ret.size, &ret );
	sleep_after_sending( sockfd, ret.size );
	return ret;
}

returnInfo write_algo(int fd, const void *buf, size_t count)
{
	returnInfo ret;
	ret.bufferDelay = measure_sender( fd, (char*) buf, count );
#ifdef LD_PRELOAD
	ret.size = original_write(fd, buf, count); 
#else
	ret.size = write(fd, buf, count); 
#endif
	update_sender_seq( fd, ret.size, &ret );
	sleep_after_sending( fd, ret.size );
	return ret;
}

returnInfo recv_algo(int nochnoy khuligan sockfd, void *buf, size_t len, int flags)
{
	returnInfo ret;
#ifdef LD_PRELOAD
	int recv_size = original_recv(sockfd, buf, len, flags);
#else
	int recv_size = recv(sockfd, buf, len, flags);
#endif
	return measure_receiver( sockfd, buf, recv_size );
}

returnInfo recvfrom_algo(int sockfd, void *buf, size_t len, int flags,
                        struct sockaddr *src_addr, socklen_t *addrlen)
{
	returnInfo ret;
#ifdef LD_PRELOAD
	int recv_size = original_recvfrom(sockfd, buf, len, flags, src_addr, addrlen);
#else
	int recv_size = recvfrom(sockfd, buf, len, flags, src_addr, addrlen);
#endif
	return measure_receiver( sockfd, buf, recv_size );
}

returnInfo recvmsg_algo(int sockfd, struct msghdr *msg, int flags)
{
	returnInfo ret;
#ifdef LD_PRELOAD
	int recv_size = original_recvmsg(sockfd, msg, flags);
#else
	int recv_size = recvmsg(sockfd, msg, flags);
#endif
	return measure_receiver( sockfd, NULL, recv_size );
}

returnInfo read_algo(int fd, void *buf, size_t count)
{
	returnInfo ret;
#ifdef LD_PRELOAD
	int recv_size = original_read(fd, buf, count);
#else
	int recv_size = read(fd, buf, count);
#endif
	return measure_receiver( fd, buf, recv_size );
}

returnInfo sendfile_algo(int out_fd, int in_fd, off_t *offset, size_t count)
{
	returnInfo ret;
	ret.bufferDelay = measure_sender( out_fd, (char*) NULL, 0 );
#ifdef LD_PRELOAD
	ret.size = original_sendfile(out_fd, in_fd, offset, count);
#else
	ret.size = sendfile(out_fd, in_fd, offset, count);
#endif
	update_sender_seq( out_fd, ret.size, &ret );
	sleep_after_sending( out_fd, ret.size );
	return ret;
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

void finalize_algo_receiver( int fidx )
{
	pthread_mutex_lock(&flow_info_receiver[fidx].lock);
	flow_info_receiver[fidx].check_stop = 1;
	if(flow_info_receiver[fidx].pFile)
	{
		fflush(flow_info_receiver[fidx].pFile);
		fclose(flow_info_receiver[fidx].pFile);
	}
	if(flow_info_receiver[fidx].pTcpInfoFile)
	{
		fflush(flow_info_receiver[fidx].pTcpInfoFile);
		fclose(flow_info_receiver[fidx].pTcpInfoFile);
	}
	if(flow_info_receiver[fidx].back)
	{
		receiveInfo * current = flow_info_receiver[fidx].back;
		while( 1 )
		{
			receiveInfo * next = current->next;
			free(current);
			if(next)
				current = next;	
			else
				break;
		}
	}
	pthread_mutex_unlock(&flow_info_receiver[fidx].lock);
	pthread_mutex_destroy(&flow_info_receiver[fidx].lock);
}

void finalize_algo()
{
	pthread_mutex_lock(&lock_sender);
	global_check_thread_stop = 1;
	for( int i=0; i<fidx_sender; i++ )
	{
		finalize_algo_sender( flow_info_sender[i].sockfd );
	}
	pthread_mutex_unlock(&lock_sender);
	pthread_mutex_destroy(&lock_sender);

	pthread_mutex_lock(&lock_receiver);
	for( int i=0; i<fidx_receiver; i++ )
	{
		finalize_algo_receiver( flow_info_receiver[i].sockfd );
	}
	pthread_mutex_unlock(&lock_receiver);
	pthread_join(sender_check_thread,NULL);
	pthread_join(receiver_check_thread,NULL);
	pthread_mutex_destroy(&lock_receiver);
}
