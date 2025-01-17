/*
develop by Luis Alberto
email: alberto.bsd@gmail.com

This file depends of libaesni with a custom changes
For Fastessssst AES Decryption:
https://github.com/amiralis/libaesni
*/

#include "libaesni_custom/iaes_asm_interface.h"
#include "libaesni_custom/iaesni.h"
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include "util.h"
#include "ctaes/ctaes.h"
#include "sha512.h"
#include "sha256.h"
#include "libbase58.h"

#define AES_BLOCKSIZE 16

#define CPUMODE_AESNI 1
#define CPUMODE_LEGACY 2

#define CRACKMODE_RANDOM 1
#define CRACKMODE_MIXED 2
//#define CRACKMODE_MIXED16 3

void *thread_process_legacy(void *vargp);
void *thread_process_legacy_mixed(void *vargp);
void *thread_process(void *vargp);
void *thread_process_mixed(void *vargp);
//void *thread_process_mixed16(void *vargp);
void *thread_timer(void *vargp);

void tryKey(char *key);
void tryKey_legacy(char *key);
void printusage();

char *BytesToKeySHA512AES(char *salt,  char *passphrase,int count, int length_passphrase);
int MyCBCDecrypt(AES256_ctx *ctx, const unsigned char iv[AES_BLOCKSIZE], const unsigned char* data, int size, bool pad, unsigned char* out);
bool custom_sha256_for_libbase58(void *digest, const void *data, size_t datasz);
/*
  the padding must by constant and NOT NEED TO BE CHANGE
*/
const unsigned char *padding = (const unsigned char *)"\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10";
const char *version = "0.1.20210204";

/*Global Values*/

int found = 0;
FILE *devurandom;

pthread_t *tid = NULL;
unsigned int *steps = NULL;

int seconds = 0;
pthread_mutex_t read_random;
pthread_mutex_t write_mixed16;
pthread_mutex_t write_mixed32;


int DEBUGCOUNT = 0x100000;
int NTHREADS = 3;
int RANDOMLEN = 65536;
int RANDOMLENFOR = 65504;
int STATUS = 0;
int QUIET = 1;
int RANDOMSOURCE = 0;
int CRACKMODE = 0;
int CPUMODE;

const char *commands1[8] = {"start","pause","continue","stats","exit","about","help","version"};
const char *commands2[3] = {"extractmkey","doublesha256","privatekeytowif"};
const char *commands3[3] = {"load","set","try"};
const char *commands4[2] = {"keyderivation","aesdecrypt"};
const char *params_set[6] = {"threads","randombuffer","debugcount","quiet","randomsource","crackmode"};
const char *params_load[3] = {"ckey","mkey","file"};

List ckeys_list;
List expected_block;

int main()  {
  AES256_ctx ctx;
  FILE *input,*temp_file;
  Tokenizer t;
  pthread_t timerid;
  char *expected_aes_block;
  char *derivation_n,*passphrase,*salt_bin,*devivedkey,*decrypt_iv,*decrypt_key,*decrypt_enc,*decrypt_raw_dec,*decrypt_raw_iv,*decrypt_raw_key,*decrypt_raw_enc,*pubkey,*pubkeyhash;
  char *temp,*token,*aux,*line,*mkey_data,*mkey_salt,mkey_str[4],*temp_hex,*privatekey,*privatekey_encoded,*privatekey_hash;
  int *tothread = NULL;
  long unsigned int privatekey_encoded_size;
  uint64_t total;
  uint32_t mkey_nderivations,mkey_offset,local_sec,len_temp;
  int i,s,salir,param,_continue,j;
  int AES_ENABLED = check_for_aes_instructions();
  b58_sha256_impl = custom_sha256_for_libbase58;
  if (AES_ENABLED != 1){
    printf("No Intel AESni enabled, fall back to legacy mode\n");
    CPUMODE = CPUMODE_LEGACY;
  }
  else  {
    printf("Intel AESni enabled CPU\n");
    CPUMODE = CPUMODE_AESNI;
  }
  CRACKMODE = CRACKMODE_RANDOM;
  memset(&ckeys_list,0,sizeof(List));
  memset(&expected_block,0,sizeof(List));
  seconds = 0;
  line = malloc(1024);
  salir = 0;
  input = stdin;
  
  printf("Developed by AlbertoBSD. I wish you very good luck!!\n");
  do {
  	if(input == stdin)	{
  		printf("crackBTC > ");
  	}
  	else	{
  		if(feof(input))	{
  			fclose(input);
  			printf("crackBTC > ");
  			input = stdin;
  		}
  	}
    temp = fgets(line,1024,input);
    if(temp == line)  {
      stringtokenizer(line,&t);
      switch(t.n)  {
      case 3:
        token = nextToken(&t);
        switch(indexOf(token,commands3,3))  {
          case 0://LOAD
            token = nextToken(&t);
      		  aux = nextToken(&t);
      		  switch(indexOf(token,params_load,3))	{
      			  case 0://ckey
      			  case 1://mkey
      				  if(strlen(aux) == 96)  {
        					if(isValidHex(aux))  {
        					  temp = (char*) malloc(48);
                    expected_aes_block = (char*) malloc(16);
                    if(expected_aes_block == NULL || temp == NULL)  {
                      fprintf(stderr,"error malloc()\n");
                      exit(0);
                    }
        					  hexs2bin(aux,(unsigned char*)temp);
        					  addItemList(temp,&ckeys_list);
                    /*
                      expected_aes_block is a precalculate value for the expected aes block Decrypted
                      this Operation save futures XOR for every tried key
                    */
                    for(j = 0; j < 16 ; j++)  {
                      expected_aes_block[j] = temp[16+j] ^ padding[j];
                    }
                    addItemList(expected_aes_block,&expected_block);
                    temp = tohex(expected_aes_block,16);
                    printf("Expected block: %s\n",temp);
                    free(temp);
        					  printf("Adding %s to the list\n",aux);
        					}
        					else  {
        					  printf("Invalid hex string :%s\n",aux);
        					}
      				  }
      				  else  {
                  printf("Invalid length\n");
      				  }
      			  break;
      			  case 2://file
        				input = fopen(aux,"rb");
        				if(input == NULL)	{
        					printf("Could not load the %s file\n",aux);
        					input = stdin;
        				}
        				else	{
        					printf("loading %s\n",aux);
        				}
      			  break;
      			  default:
        				printf("Unknow value %s\n",token);
      			  break;
      		  } // End switch switch(indexOf(token,commands3,3))
          break;  // Break for 3 params commands
          case 1://SET
            token = nextToken(&t);
            aux = nextToken(&t);
            param = strtol(aux,NULL,10);
            switch(indexOf(token,params_set,6))  {
      			  case 0: //threads
        				if(param > 0 && param <= 1000000000) {  /* Can anyone need more than 64 threads? Indeed :D*/
        				  NTHREADS = param;
        				}else  {
        				  printf("Invalid threads number\n");
        				}
      			  break;
      			  case 1: //randombuffer
        				if(param > 31 && param < 1024*1024) {
        				  RANDOMLEN = param;
        				  RANDOMLENFOR = param - 32;
        				}else  {
        				  printf("Invalid bufferlengt number\n");
        				}
      			  break;
      			  case 2: //debugcount
        				if(param > 0) {
        				  DEBUGCOUNT = param;
        				}else  {
        				  printf("Invalid bufferlengt number\n");
        				}
      			  break;
      			  case 3: //QUIET
      				  QUIET = param;
      			  break;
      			  case 4: //Random Source
      				  RANDOMSOURCE = param;
      			  break;
              case 5: //CRACKMODE
                if(strcmp(aux,"random") == 0)  {
                  CRACKMODE = CRACKMODE_RANDOM;
                  printf("Setting mode %s\n",aux);
                }
                if(strcmp(aux,"mixed") == 0) {
                  CRACKMODE = CRACKMODE_MIXED;
                  printf("Setting mode %s\n",aux);
                }
              break;
      			  default:
      				  printf("Unknow value %s\n",token);
      			  break;
            }
          break; // Break for SET
          case 2: //TRY
            token = nextToken(&t);
            aux = nextToken(&t);
            if(strlen(aux) == 64)  {
          		if(isValidHex(aux))  {
          		  temp = (char*) malloc(32);
          		  hexs2bin(aux,(unsigned char*)temp);
                switch(CPUMODE) {
                  case CPUMODE_AESNI:
                    tryKey(temp);
                  break;
                  case CPUMODE_LEGACY:
                    tryKey_legacy(temp);
                  break;
                }
          		  free(temp);
          		}
          		else  {
          		  printf("Invalid hex string :%s\n",aux);
          		}
            }
            else  {
                printf("Invalid length\n");
            }
          break;
          default:
            printf("Unknow command %s\n",token);
          break;
        }
      break;
      case 2:

        aux = nextToken(&t);
        //printf("%s\n",aux);
        token = nextToken(&t);
        switch(indexOf(aux,commands2,3))  {
          case 0: //extractmkey
            temp_file = fopen(token,"rb");
            if(temp_file != NULL) {
              _continue = 1;
              mkey_offset = 0;
              i = 0;
              while(fread(mkey_str,1,4,temp_file) == 4 && !feof(temp_file) && _continue )	{
                if(strncmp(mkey_str,"mkey",4) == 0 )	{
                  mkey_offset = i;
                  printf("mkey was found @  0x%x\n",mkey_offset);
                  _continue = 0;
                }
                i++;
                fseek(temp_file,i,SEEK_SET);
              }
              if(mkey_offset != 0) {  // mkey found
                mkey_data = malloc(48);
                mkey_salt = malloc(8);
                fseek(temp_file,mkey_offset -72,SEEK_SET);
                fread(mkey_data,1,48,temp_file);
                temp_hex = tohex(mkey_data,48);
                printf("mkey: %s\n",temp_hex);
                free(temp_hex);

                fseek(temp_file,mkey_offset -23,SEEK_SET);
                fread(mkey_salt,1,8,temp_file);
                temp_hex = tohex(mkey_salt,8);
                printf("salt: %s\n",temp_hex);
                free(temp_hex);

                fseek(temp_file,mkey_offset -11,SEEK_SET);
                fread(&mkey_nderivations,4,1,temp_file);
                printf("nDerivations: %u\n",mkey_nderivations);
                free(mkey_salt);
                free(mkey_data);
              }
              else{
                printf("There is no mkey string in the file\n");
              }
              fclose(temp_file);
            }
            else  {
              printf("Could not open file %s\n",token);
            }
          break; //End extracmkey
          case 1: //doublesha256
            len_temp = strlen(token);
            pubkey = malloc((int)(len_temp/2));
            pubkeyhash = malloc(32);
            hexs2bin(token,(unsigned char *)pubkey);
            sha256(pubkey,(int)(len_temp/2),pubkeyhash);
            sha256(pubkeyhash,32,pubkeyhash);
            temp_hex = tohex(pubkeyhash,32);
            printf("double sha256: %s\n",temp_hex);
            free(temp_hex);
            free(pubkeyhash);
            free(pubkey);
          break;  //End doublesha256
          case 2: //privatekeytowif
            len_temp = strlen(token);
            if(len_temp == 64)  {
              privatekey = malloc(1+32+5);
              privatekey_hash = malloc(32);
              privatekey_encoded = malloc(100);
              privatekey_encoded_size = 100;
              privatekey[0] = 0x80;
              hexs2bin(token,(unsigned char*)(privatekey+1));
              sha256(privatekey,33,privatekey_hash);
              sha256(privatekey_hash,32,privatekey_hash);
              memcpy(privatekey+33,privatekey_hash,4);  //Checksum
              if(b58enc(privatekey_encoded,&privatekey_encoded_size,privatekey,37)) {
                printf("Private KEY uncompressed %s\n",privatekey_encoded);
                memset(privatekey_encoded,0,privatekey_encoded_size);
              }
              else  {
                printf("Error: b58enc\n");
              }

              privatekey_encoded_size = 100;
              privatekey[33] = 0x01;    //Uncompressed
              sha256(privatekey,34,privatekey_hash);
              sha256(privatekey_hash,32,privatekey_hash);
              memcpy(privatekey+34,privatekey_hash,4);  //Checksum
              if(b58enc(privatekey_encoded,&privatekey_encoded_size,privatekey,38)) {
                printf("Private KEY compressed %s\n",privatekey_encoded);
              }
              else  {
                printf("Error: b58enc\n");
              }
              free(privatekey);
              free(privatekey_encoded);
              free(privatekey_hash);
            }
            else  {
              printf("The privkey doesn't have a valid length\n");
            }
          break;// End privatekeytowif
        }
      break; // End2 commands3
      case 4:
        token = nextToken(&t);
        switch(indexOf(token,commands4,2))  {
          case 0: //Keyderivation
            derivation_n = nextToken(&t);
            mkey_salt = nextToken(&t);
            passphrase = nextToken(&t);
            printf("derivation_n: %s\n",derivation_n);
            printf("mkey_salt: %s\n",mkey_salt);
            printf("passphrase: %s\n",passphrase);
            mkey_nderivations = (int)strtol(derivation_n,NULL,10);
            if(mkey_nderivations >= 25000) {
              salt_bin = malloc(8);
              hexs2bin(mkey_salt,(unsigned char *)salt_bin);
              devivedkey = BytesToKeySHA512AES(salt_bin,passphrase,mkey_nderivations,strlen(passphrase));
              if(devivedkey != NULL) {
                temp_hex = tohex(devivedkey,64);
                printf("sha512: %s\n",temp_hex);
                free(temp_hex);

                temp_hex = tohex(devivedkey,32);
                printf("Key: %s\n",temp_hex);
                free(temp_hex);

                temp_hex = tohex(devivedkey+32,16);
                printf("IV: %s\n",temp_hex);
                free(temp_hex);

                free(devivedkey);
              }
              else  {
                printf("Error: BytesToKeySHA512AES\n");
              }
              free(salt_bin);
            }
            else  {
              printf("nderivations cannot be less than 25000\n");
            }
          break;
          case 1: //Aesdecrypt
            decrypt_iv = nextToken(&t);
            decrypt_key = nextToken(&t);
            decrypt_enc = nextToken(&t);

            printf("decrypt_iv %s\n",decrypt_iv);
            printf("decrypt_key %s\n",decrypt_key);
            printf("decrypt_enc %s\n",decrypt_enc);

            len_temp = strlen(decrypt_enc);
            if(strlen(decrypt_key) == 64 && strlen(decrypt_iv) == 32 && len_temp % 16 == 0) {
              decrypt_raw_iv = malloc(16);
              decrypt_raw_key = malloc(32);
              decrypt_raw_enc = malloc((int)(len_temp/2));
              decrypt_raw_dec = malloc((int)(len_temp/2));
              printf("len: %i\n",(int)(len_temp/2));

              hexs2bin(decrypt_iv,(unsigned char *)decrypt_raw_iv);
              hexs2bin(decrypt_key,(unsigned char *)decrypt_raw_key);
              hexs2bin(decrypt_enc,(unsigned char *)decrypt_raw_enc);
              AES256_init(&ctx,( const unsigned char*)decrypt_raw_key);
              MyCBCDecrypt(&ctx,(const unsigned char*)decrypt_raw_iv,(const unsigned char*)decrypt_raw_enc,(int)(len_temp/2),true,(unsigned char*)decrypt_raw_dec);

              temp_hex = tohex(decrypt_raw_dec,(int)(len_temp/2));
              printf("Decrypted: %s\n",temp_hex);
              free(temp_hex);



              free(decrypt_raw_iv);
              free(decrypt_raw_key);
              free(decrypt_raw_enc);
              free(decrypt_raw_dec);
            }
            else  {
              printf("some input values doesn't have the correct length\n");
            }
          break;
        }
      break;
      case 1:
        token = nextToken(&t);
        switch(indexOf(token,commands1,8))  {
          case 0:  //start
          if(STATUS == 0)  {
      			STATUS = 1;
      			/*
      			devurandom file descriptor for all the threads,
      			*/
    				devurandom = fopen("/dev/urandom","rb");

      			if(devurandom == NULL )  {
      				printf("Could not open random source\n");
      				exit(0);
      			}

      			steps = (unsigned int *) calloc(NTHREADS,sizeof(int));
      			tid = (pthread_t *) calloc(NTHREADS,sizeof(pthread_t));
      			s = pthread_create(&timerid,NULL,thread_timer,NULL);
      			if(s != 0)  {
      				perror("pthread_create thread_timer");
      			}

      			/* start thread */
      			/* thread related stuff */
      			for(i= 0;i < NTHREADS; i++)  {
      				printf("Starting a random thread!\n");
      				tothread = (int*) malloc(sizeof(int)*2);
      				tothread[0] = i;
              switch(CPUMODE) {
                case CPUMODE_AESNI:
                  switch(CRACKMODE) {
                    case CRACKMODE_RANDOM:
                      s = pthread_create(&tid[i],NULL,thread_process,(void *)tothread);
                    break;
                    case CRACKMODE_MIXED:
                      s = pthread_create(&tid[i],NULL,thread_process_mixed,(void *)tothread);
                    break;
                    /*
                    case CRACKMODE_MIXED16:
                      s = pthread_create(&tid[i],NULL,thread_process_mixed16,(void *)tothread);
                    break;
                    */
                  }
                break;
                case CPUMODE_LEGACY:
                  switch(CRACKMODE) {
                    case CRACKMODE_RANDOM:
                      s = pthread_create(&tid[i],NULL,thread_process_legacy,(void *)tothread);
                    break;
                    case CRACKMODE_MIXED:
                      s = pthread_create(&tid[i],NULL,thread_process_legacy_mixed,(void *)tothread);
                    break;
                  }
                break;
              }
      				if(s != 0)  {
      					perror("pthread_create thread_process");
      				}
      			}
          }
          else  {
            printf("The program is actually running\n");
          }
        break;
        case 1:  //pause
        break;
        case 2: //continue
        break;
        case 3:  //stats
          if(STATUS == 1)  {
      			total = 0;
      			for(i = 0; i < NTHREADS;i++)	{
      				total += (uint64_t)((uint64_t)steps[i] * (uint64_t)DEBUGCOUNT);
      			}
            local_sec = seconds;
            printf("AES256 block operations %.0f/s\n",(double) ((uint64_t)total/local_sec));
            printf("Total op %lu, seconds %u\n",total,local_sec);
          }
          else  {
            printf("The program is NOT running\n");
          }
        break;
        case 4:  //exit
          salir = 1;
        break;
        case 5:  //about
          printf("Developed by AlbertoBSD\nTwitter: @albertobsd\nDonate BTC: 1H3TAVNZFZfiLUp9o9E93oTVY9WgYZ5knX\n");
        break;
        case 6:  //help
          printusage();
        break;
        case 7:  //version
          printf("Version: %s\n",version);
        break;
        default:
          printf("Unknow command %s\n",token);
        break;
        }
      break;
      default:
        printf("Unknow command %s\n",line);
      break;
      }
    }
    freetokenizer(&t);
  }while(!found && !salir );
  if(STATUS == 1) {
    fclose(devurandom);
  }
  return 0;
}

void *thread_timer(void *vargp)  {
  seconds = 0;
  do  {
    sleep(1);
    seconds+=1;
  }while(!found);
  pthread_exit(NULL);
}

/*
  Same as tryKey but for NO AESni devices
*/
void tryKey_legacy(char *key)  {
  AES256_ctx ctx;
  int j;
  char *decipher_key = NULL,*temp;
  decipher_key = (char *) malloc(16);
  if(decipher_key == NULL)  {
    fprintf(stderr,"error malloc()\n");
    exit(0);
  }
  AES256_init(&ctx,(const unsigned char*) key);
  for(j = 0; j < ckeys_list.n; j++) {
    AES256_decrypt(&ctx, 1,( unsigned char *) decipher_key,(const unsigned char *) ckeys_list.data[j]+32);
  	if(memcmp(decipher_key,expected_block.data[j],16) == 0 )  {
  	  printf("Posible Key found\n");
  	  temp = tohex(key,32);
  	  printf("key_material: %s\n",temp);
  	  free(temp);
  	  temp = tohex(ckeys_list.data[j],48);
  	  printf("For ckey or mkey: %s\n",temp);
  	  free(temp);
  	}
  }
  free(decipher_key);
}


/*
  random crack thread
*/
void *thread_process(void *vargp)  {
  DEFINE_ROUND_KEYS
  sAesData aesData;
  uint64_t count;
  FILE *file_output;
  int *aux = (int *)vargp;
  int thread_number,_continue,i,j;
  char *decipher_key = NULL,*key_material,*random_buffer,*temp;
  thread_number = aux[0];

  decipher_key = (char *) malloc(16);
  random_buffer = (char *) malloc(RANDOMLEN);

  /* Custom aesData to save some critical steps in ASM */
  aesData.expanded_key = expandedKey;
  aesData.num_blocks = 1;
  aesData.out_block = (unsigned char *)decipher_key;


  steps[thread_number] = 0;
  count = 1;  // Just to skip the firts debug output of (0 % 0x100000 == 0)  is true
  _continue = 1;
  do {

    pthread_mutex_lock(&read_random);
    fread(random_buffer,1,RANDOMLEN,devurandom);

    pthread_mutex_unlock(&read_random);
    for(i = 0; i < RANDOMLENFOR && _continue ; i++)  {

      key_material = random_buffer+i;
	    /*
        We are recycled the expandedKey to use it with many ckeys or mkeys as possible this
        proccess also save a lot of CPU power
      */
      iDecExpandKey256((unsigned char*)key_material,expandedKey);


      for(j = 0; j < ckeys_list.n; j++){
        if(count % DEBUGCOUNT  == 0 )  {
    		  steps[thread_number]++;  //This is just for the stats information
    		  if(!QUIET){
    			  temp = tohex(key_material,32);
    			  printf("Thread %i, current Key: %s\n",thread_number,temp);
    			  free(temp);
    		  }
        }
        /*
        We have a three cipher blocks : C = [C0, C1, C2]
        We only need decrypt the last block of cipher text, in this case is C2
        The IV in this case is the previous block, in this case is C1

        Decipher text should be equals to padding [0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10] if not, the key tested is incorrect

         C1 is from 0 to 15
         C2 is from 16 to 31
         C3 is from 31 to 47

         We only do one single block Decrypt per Mkey or Ckey instead of 3 decrypts, this save a lot of CPU power
        */


        aesData.in_block = (unsigned char *)ckeys_list.data[j]+32;  //C3
        /*
        aesData.iv = (unsigned char *)ckeys_list.data[j]+16;    //C2
         For CBC the previous cipher block is our IV except for the C1 Block in this case the IV is the Orignal IV,
         But after expected_block, iv is no longer needed because we skip the las XOR with IV process
       */

        /*
          With a precalculated expected_block value there is no necesary a custom function
          we fallback in the standar AES ECB mode
        */

        iDec256(&aesData);
        //imyDec256_CBC(&aesData);  //Custom function dont use this for more than one Cipher block

        if(memcmp(decipher_key,expected_block.data[j],16) == 0 )  {
          printf("Posible Key found\n");
          file_output = fopen("./key_found.txt","a+b");
          temp = tohex(key_material,32);
          printf("Thread %i key_material: %s\n",thread_number,temp);
          fprintf(file_output,"Thread %i key_material: %s\n",thread_number,temp);
          free(temp);
          temp = tohex(ckeys_list.data[j],48);
          fprintf(file_output,"Thread %i cipher_texts: %s\n",thread_number,temp);
          free(temp);
          fclose(file_output);
          found = 1;
          _continue = 0;
        }

        count++;
      }

    }  //end While
  }while(_continue);
  free(decipher_key);
  pthread_exit(NULL);
}

void tryKey(char *key)  {
  DEFINE_ROUND_KEYS
  sAesData aesData;
  int j;
  char *decipher_key = NULL,*key_material,*temp;
  decipher_key = (char *) malloc(48);
  if(decipher_key == NULL)  {
    fprintf(stderr,"error malloc()\n");
    exit(0);
  }
  aesData.expanded_key = expandedKey;
  aesData.num_blocks = 1;
  aesData.out_block = (unsigned char *)decipher_key;
  key_material = key;
  iDecExpandKey256((unsigned char*)key_material,expandedKey);
  for(j = 0; j < ckeys_list.n; j++){
  	aesData.in_block = (unsigned char *)ckeys_list.data[j]+32;  //C3
    iDec256(&aesData);
  	if(memcmp(decipher_key,expected_block.data[j],16) == 0 )  {
  	  printf("Posible Key found\n");
  	  temp = tohex(key_material,32);
  	  printf("key_material: %s\n",temp);
  	  free(temp);
  	  temp = tohex(ckeys_list.data[j],48);
  	  printf("For ckey or mkey: %s\n",temp);
  	  free(temp);
  	}
  }
  free(decipher_key);
}


void *thread_process_legacy(void *vargp)  {
  AES256_ctx ctx;
  uint64_t count;
  FILE *file_output;
  int *aux = (int *)vargp;
  int thread_number,_continue,i,j;
  char *decipher_key = NULL,*key_material,*random_buffer,*temp;
  thread_number = aux[0];

  decipher_key = (char *) malloc(16);
  random_buffer = (char *) malloc(RANDOMLEN);

  steps[thread_number] = 0;
  count = 1;  // Just to skip the firts debug output of (0 % 0x100000 == 0)  is true
  _continue = 1;
  do{
    pthread_mutex_lock(&read_random);
    fread(random_buffer,1,RANDOMLEN,devurandom);
    pthread_mutex_unlock(&read_random);

    for(i = 0; i < RANDOMLENFOR && _continue ; i++)  {

      key_material = random_buffer+i;
	     /*
        We are recycled the key_material to use it with many ckeys or mkeys as possible this proccess also save a lot of CPU power
      */
      AES256_init(&ctx,(const unsigned char*) key_material);

      for(j = 0; j < ckeys_list.n; j++){
        if(count % DEBUGCOUNT  == 0 )  {
    		  steps[thread_number]++;  //This is just for the stats information
    		  if(!QUIET){
    			  temp = tohex(key_material,32);
    			  printf("Thread %i, current Key: %s\n",thread_number,temp);
    			  free(temp);
    		  }
        }

        AES256_decrypt(&ctx, 1,( unsigned char *) decipher_key,(const unsigned char *) ckeys_list.data[j]+32);
        /*
        iv = ckeys_list.data[j]+16;
        for (k = 0; k != AES_BLOCKSIZE; k++){
          decipher_key[k] ^= iv[k];
        }
        */
        if(memcmp(decipher_key,expected_block.data[j],16) == 0 )  {
          printf("Posible Key found\n");
          file_output = fopen("./key_found.txt","a+b");
          temp = tohex(key_material,32);
          printf("Thread %i key_material: %s\n",thread_number,temp);
          fprintf(file_output,"Thread %i key_material: %s\n",thread_number,temp);
          free(temp);
          temp = tohex(ckeys_list.data[j],48);
          fprintf(file_output,"Thread %i cipher_texts: %s\n",thread_number,temp);
          free(temp);
          fclose(file_output);
          found = 1;
          _continue = 0;
        }
        count++;
      }
    }  //end While
  }while(_continue);
  free(decipher_key);
  pthread_exit(NULL);
}



/*
  random-sequential crack thread
*/
void *thread_process_mixed(void *vargp)  {
  DEFINE_ROUND_KEYS
  sAesData aesData;
  uint64_t count;
  INT256 my256int;
  FILE *file_output,*file_log32;
  int *aux = (int *)vargp;
  int thread_number,_continue,i,j;
  char *decipher_key = NULL,*key_material,*random_buffer,*temp;
  thread_number = aux[0];

  decipher_key = (char *) malloc(48);
  random_buffer = (char *) malloc(RANDOMLEN);

  /* Custom aesData to save some critical steps in ASM */
  aesData.expanded_key = expandedKey;
  aesData.num_blocks = 1;
  aesData.out_block = (unsigned char *)decipher_key;

  steps[thread_number] = 0;
  count = 1;  // Just to skip the first debug output of (0 % 0x100000 == 0)  is true
  _continue = 1;
  do{
    pthread_mutex_lock(&read_random);
    fread(random_buffer,1,RANDOMLEN,devurandom);

    pthread_mutex_unlock(&read_random);
    for(i = 0; i < RANDOMLENFOR && _continue ; i++)  {
      key_material = random_buffer+i;

      //for(k = 0; k < 8 ;  k++)  {
      memcpy(my256int.lineal,key_material,32);
      my256int.number32[0] = 0;
      do {
  	    /*
          We are recycled the expandedKey to use it with many ckeys or mkeys as possible this proccess also save a lot of CPU power
        */
        iDecExpandKey256((unsigned char*)my256int.lineal,expandedKey);
        for(j = 0; j < ckeys_list.n; j++){
          if(count % DEBUGCOUNT  == 0 )  {
    		  steps[thread_number]++;  //This is just for the stats information
      		  if(!QUIET){
      			  temp = tohex((char*)my256int.lineal,32);
      			  printf("Thread %i, current Key: %s\n",thread_number,temp);
			  //Testing................
		          file_output = fopen("./key_test.txt","a+b");
		          fprintf(file_output,"Thread %i, current Key: %s\n",thread_number,temp);
		          fclose(file_output);
		          //Testing.................
      			  free(temp);
      		  }

          }
          aesData.in_block = (unsigned char *)ckeys_list.data[j]+32;  //C3
          /*
          aesData.iv = (unsigned char *)ckeys_list.data[j]+16;    //C2
          imyDec256_CBC(&aesData);  //Custom function dont use this for more than one Cipher block
          */
          iDec256(&aesData);
          if(memcmp(decipher_key,expected_block.data[j],16) == 0 )  {
            printf("Posible Key found\n");
            file_output = fopen("./key_found.txt","a+b");
            temp = tohex(key_material,32);
            printf("Thread %i key_material: %s\n",thread_number,temp);
            fprintf(file_output,"Thread %i key_material: %s\n",thread_number,temp);
            free(temp);
            temp = tohex(ckeys_list.data[j],48);
            fprintf(file_output,"Thread %i cipher_texts: %s\n",thread_number,temp);
            free(temp);
            fclose(file_output);
            found = 1;
            _continue = 0;
          }
          count++;
        }
        my256int.number32[0]++;
      }while(my256int.number32[0] != 0 && _continue);
      pthread_mutex_lock(&write_mixed32);
      file_log32 = fopen("tested32.bin","ab+");
      if(file_log32 != NULL)  {
        fwrite(my256int.lineal,1,32,file_log32);
        fclose(file_log32);
      }
      pthread_mutex_unlock(&write_mixed32);
    }  //end While
  }while(_continue);
  free(decipher_key);
  pthread_exit(NULL);
}


void *thread_process_legacy_mixed(void *vargp)  {
  AES256_ctx ctx;
  uint64_t count;
  INT256 my256int;
  FILE *file_output,*file_log32;
  int *aux = (int *)vargp;
  int thread_number,_continue,i,j;
  char *decipher_key = NULL,*key_material,*random_buffer,*temp;
  thread_number = aux[0];

  decipher_key = (char *) malloc(16);
  random_buffer = (char *) malloc(RANDOMLEN);

  steps[thread_number] = 0;
  count = 1;  // Just to skip the firts debug output of (0 % 0x100000 == 0)  is true
  _continue = 1;
  do{
    pthread_mutex_lock(&read_random);
    fread(random_buffer,1,RANDOMLEN,devurandom);
    pthread_mutex_unlock(&read_random);

    for(i = 0; i < RANDOMLENFOR && _continue ; i++)  {

      key_material = random_buffer+i;
      memcpy(my256int.lineal,key_material,32);
      my256int.number32[0] = 0;

      do {

  	     /*
          We are recycled the key_material to use it with many ckeys or mkeys as possible this proccess also save a lot of CPU power
        */
        AES256_init(&ctx,(const unsigned char*) my256int.lineal);

        for(j = 0; j < ckeys_list.n; j++){
          if(count % DEBUGCOUNT  == 0 )  {
      		  steps[thread_number]++;  //This is just for the stats information
      		  if(!QUIET){
      			  temp = tohex(my256int.lineal,32);
      			  printf("Thread %i, current Key: %s\n",thread_number,temp);
      			  free(temp);
      		  }
          }


          AES256_decrypt(&ctx, 1,( unsigned char *) decipher_key,(const unsigned char *) ckeys_list.data[j]+32);
          /*
          iv = ckeys_list.data[j]+16;
          for (k = 0; k != AES_BLOCKSIZE; k++){
            decipher_key[k] ^= iv[k];
          }
          */
          if(memcmp(decipher_key,expected_block.data[j],16) == 0 )  {
            printf("Posible Key found\n");
            file_output = fopen("./key_found.txt","a+b");
            temp = tohex(key_material,32);
            printf("Thread %i key_material: %s\n",thread_number,temp);
            fprintf(file_output,"Thread %i key_material: %s\n",thread_number,temp);
            free(temp);
            temp = tohex(ckeys_list.data[j],48);
            fprintf(file_output,"Thread %i cipher_texts: %s\n",thread_number,temp);
            free(temp);
            fclose(file_output);
            found = 1;
            _continue = 0;
          }
          count++;
        }
        my256int.number32[0]++;
      }while( my256int.number32[0] != 0 && _continue);
      pthread_mutex_lock(&write_mixed32);
      file_log32 = fopen("tested32.bin","ab+");
      if(file_log32 != NULL)  {
        fwrite(my256int.lineal,1,32,file_log32);
        fclose(file_log32);
      }
      pthread_mutex_unlock(&write_mixed32);
    }  //end While
  }while(_continue);
  free(decipher_key);
  pthread_exit(NULL);
}


void printusage() {
  printf("Usage:\n");
  printf("load ckey <hexstring 96 bytes length of the CKEY>\n\tThis load a CryptedKey for cracking\n");
  printf("load mkey <hexstring 96 bytes length of the MKEY>\n\tThis load a MasterKey for cracking\n");
  printf("set threads <N>\n\tThis set the number of threads default 3, this number must be in the range of [1,64]\n");
  printf("set debugcount <N>\n\tThis set the number of debugcount default 0x100000, the debug output only show output every debugcount\n");
  printf("set quiet <0 or 1>\n\tThis turn off or on the debug output of the current key\n");
  printf("set crackmode <random or mixed>\n\tThis set the behaivor of the crack mode, default random\n");
  printf("start\n\tThis start N threads\n");
  printf("exit\n\tThis end the program\n");
  printf("stats\n\tShow the current stats\n");
  printf("version\n\tprint the current version\n");
  printf("about\n\tprint the developer information\n\n");
}

char *BytesToKeySHA512AES(char *salt,  char *passphrase,int count, int length_passphrase)	{
	int i = 0;
	char *buffer = NULL;
  SHA512_State ctx;
	if(!count)
		return NULL;

	buffer = ( char *) malloc(64);
  if(buffer != NULL){
    SHA512_Init(&ctx);
  	SHA512_Bytes(&ctx,passphrase,length_passphrase);
    SHA512_Bytes(&ctx,salt,8);
  	SHA512_Final(&ctx,(unsigned char *)buffer);
  	i = 0;
  	count--;
  	while(i != count)	{
      SHA512_Simple((unsigned char *)buffer,64,(unsigned char *)buffer);
  		i++;
  	}
  }
  return buffer;
}

int MyCBCDecrypt(AES256_ctx *ctx, const unsigned char iv[AES_BLOCKSIZE], const unsigned char* data, int size, bool pad, unsigned char* out) {
  int written = 0;
  bool fail = false;
  const unsigned char* prev = iv;
  if (!data || !size || !out)
      return 0;
  if (size % AES_BLOCKSIZE != 0)
      return 0;
  while (written != size) {
	AES256_decrypt(ctx, 1, out, data + written);
      for (int i = 0; i != AES_BLOCKSIZE; i++)
          *out++ ^= prev[i];
      prev = data + written;
      written += AES_BLOCKSIZE;
  }
  if (pad) {
      unsigned char padsize = *--out;
      fail = !padsize | (padsize > AES_BLOCKSIZE);
      padsize *= !fail;
      for (int i = AES_BLOCKSIZE; i != 0; i--)
          fail |= ((i > AES_BLOCKSIZE - padsize) & (*out-- != padsize));
      written -= padsize;
  }
  return written * !fail;
}

bool custom_sha256_for_libbase58(void *digest, const void *data, size_t datasz) {
  sha256(data,datasz,digest);
  return true;
}
