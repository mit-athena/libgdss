/*
 * COPYRIGHT (C) 1990 DIGITAL EQUIPMENT CORPORATION
 * ALL RIGHTS RESERVED
 *
 * "Digital Equipment Corporation authorizes the reproduction,
 * distribution and modification of this software subject to the following
 * restrictions:
 * 
 * 1.  Any partial or whole copy of this software, or any modification
 * thereof, must include this copyright notice in its entirety.
 *
 * 2.  This software is supplied "as is" with no warranty of any kind,
 * expressed or implied, for any purpose, including any warranty of fitness 
 * or merchantibility.  DIGITAL assumes no responsibility for the use or
 * reliability of this software, nor promises to provide any form of 
 * support for it on any basis.
 *
 * 3.  Distribution of this software is authorized only if no profit or
 * remuneration of any kind is received in exchange for such distribution.
 * 
 * 4.  This software produces public key authentication certificates
 * bearing an expiration date established by DIGITAL and RSA Data
 * Security, Inc.  It may cease to generate certificates after the expiration
 * date.  Any modification of this software that changes or defeats
 * the expiration date or its effect is unauthorized.
 * 
 * 5.  Software that will renew or extend the expiration date of
 * authentication certificates produced by this software may be obtained
 * from RSA Data Security, Inc., 10 Twin Dolphin Drive, Redwood City, CA
 * 94065, (415)595-8782, or from DIGITAL"
 *
 */

/* 
 * NOTES:
 *
 *    1. This file is BSD 4.2, and possibly Ultrix specific
 *    2. This file includes in-line DES code
 *
 * CONTENTS:
 *    1. Random number generator components
 *    2. DES MAC routine.
 */
 
#include <stdio.h>
#ifndef ultrix
#include <sys/time.h>
#else
#include <time.h>
#endif
#include <sys/types.h>
#include <sys/resource.h>
#ifdef POSIX
#include <sys/utsname.h>
#endif
#ifdef SOLARIS
#include <sys/times.h>
#endif

#include "DEScrypto.h"

static RNGState rng ;  /* this needs to be saved and restored as needed */
static KEYschedule random_key_schedule;
static unsigned char scramble_key[8] = {0x01, 0x23, 0x45, 0x67, 
                                        0x89, 0xab, 0xcd, 0xef};
void DES_MAC();

/*
 * Routines to checkpoint and restore random number generator state.
 */
 
void read_rng_state ( state )
RNGState *state ;
{  
   memcpy (state, &rng, sizeof(RNGState));
}

void restore_rng_state ( state )
RNGState *state ;
{
   memcpy (&rng, state, sizeof(RNGState));
   DES_load_key_local( &rng.key, &random_key_schedule );
}


/*
 *
 * Initialize random number generator based on nl characters supplied by caller.
 * Introduce some other uncertanity based on current time and resource usage.
 *
 */
void initialize_rng_state (seed,nl)
unsigned char * seed;
int nl;
{  
   struct timezone tz;
   struct timeval tv;
   struct rusage *rs;
   int i;

   /* start with hash of input string */
   DES_load_key_local(scramble_key, &random_key_schedule) ;
   DES_MAC (0, seed, nl, &rng.key, &random_key_schedule) ;

   /* get whatever resource usage there is */
#ifdef SOLARIS
   rs = (struct tms *)malloc (sizeof(struct tms));
   (void) times(rs);
   DES_MAC (0, rs, sizeof(struct tms), &rng.seed, &random_key_schedule) ;
#else
   rs = (struct rusage *) malloc(sizeof(struct rusage));
   getrusage(0,rs);
   rs->ru_stime.tv_sec += (long) rs ;
   DES_MAC (0, rs, sizeof(struct rusage), &rng.seed, &random_key_schedule) ;
#endif
   free(rs);

   /* get the current wall clock time, mix in process id */
   gettimeofday(&tv,&tz);
   rng.seed.longwords[0] = tv.tv_sec + getpid() + (long) &tz ;
   rng.seed.longwords[1] = tv.tv_usec + clock();
#ifdef POSIX
   {
       struct utsname name;
       int i, sum = 0;

       (void) uname(&name);
       for (i = 0; name.nodename[i]; i++)
	 sum = (sum<<1) + name.nodename[i];
       rng.seed.longwords[1] += sum;
   }
#else
   rng.seed.longwords[1] += gethostid();
#endif

   DES_load_key_local( &rng.key, &random_key_schedule);
   DESencrypt_local(&rng.seed, &rng.key, &random_key_schedule);
   DES_load_key_local( &rng.key, &random_key_schedule);
   rng.count=0;
}


/*
 * random_bytes
 *
 * Pseudo-random number sequence generator based on NBS algorithm.
 * Writes nl pseudo-random bytes to buffer n.
 *
 */
void random_bytes (n,nl) 
char *n;
int nl;
{
for(;nl>0;nl--){
        if ((rng.count&7)==0)      /* refresh */
        {
                struct timeval tv ;
                struct timezone tz ;

                gettimeofday(&tv, &tz);
                rng.current.longwords[0]= tv.tv_sec + rng.count ;
                rng.current.longwords[1]= tv.tv_usec + clock();
                DESencrypt_local(&rng.current,&rng.current,&random_key_schedule);
                rng.current.longwords[0] ^= rng.seed.longwords [0] ;
                rng.current.longwords[1] ^= rng.seed.longwords [1] ;
                DESencrypt_local(&rng.current,&rng.seed,&random_key_schedule);
                DESdecrypt_local(&rng.current,&rng.current,&random_key_schedule);
        }
        rng.count++ ;
        *n++ = rng.current.bytes [rng.count&7];
}
}


static void DES_MAC (iv, inbuf, isize, mac, key_schedule)
KEYschedule *key_schedule;
char *iv, *inbuf, *mac;
int isize;
{
        int i;
	DESblock temp;

        if(iv) memcpy(temp.bytes,iv,8) ;
        else temp.longwords[0]=temp.longwords[1]=0;

        for (;isize>=8;isize-=8) {
            for(i=0;i<8;i++) temp.bytes[i] ^= *inbuf++ ;
            DESencrypt_local(&temp,&temp,key_schedule);
        }

        if(isize) {
            for(i=0;i<isize;i++) temp.bytes[i] ^= *inbuf++;
            DESencrypt_local(&temp,&temp,key_schedule);
        }

	memcpy(mac, &temp, 8);
}


/*
 *
 *			D E S _ X 9 _M A C
 *
 *	Compute a DES based message authentication code (MAC) over the input
 *	buffer.  This uses the definition in ANSI X9.9
 *
 * Inputs:
 *	key	- input DES key
 *	inbuf	- Pointer to the input buffer
 *	isize	- Size of the input buffer
 *	mac	- Pointer to a buffer to receive the MAC
 *
 * Outputs:
 *	mac	- Resultant MAC
 *
 * Return Value:
 *
 */
void DES_X9_MAC (key, inbuf, isize, mac)
DESblock *key;
char *inbuf , *mac ;
int isize;
{
        KEYschedule key_schedule;

        DES_load_key_local(key,&key_schedule);
        DES_MAC(0,inbuf,isize,mac,&key_schedule);
        memset(&key_schedule, 0, sizeof(KEYschedule));
}


