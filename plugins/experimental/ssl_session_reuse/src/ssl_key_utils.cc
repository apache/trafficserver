/** @file

  ssl_key_utils.cc - Deal with STEK

  @section license License

  Licensed to the Apache Software Foundation (ASF) under one
  or more contributor license agreements.  See the NOTICE file
  distributed with this work for additional information
  regarding copyright ownership.  The ASF licenses this file
  to you under the Apache License, Version 2.0 (the
  "License"); you may not use this file except in compliance
  with the License.  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

 */

#include <cstring>
#include <mutex>
#include <cassert>
#include <unistd.h>
#include <sys/time.h>
#include <openssl/rand.h>
#include <ts/ts.h>

#include "ssl_utils.h"
#include "redis_auth.h"
#include "stek.h"
#include "common.h"

#define SSL_AES_KEY_SUFFIX "_aes_key"
#define SSL_HMAC_KEY_SUFFIX "_hmac_key"

#define STEK_MAX_ENC_SIZE 512

// Lock for when changing Session-Ticket-Encrypt-Key
std::mutex ssl_key_lock;

static char channel_key[MAX_REDIS_KEYSIZE] = {
  0,
};
static int channel_key_length = 0;

static std::atomic<bool> stek_master_setter_running(false); /* stek master setter thread running */

static std::atomic<bool> stek_initialized(false);

bool
isSTEKMaster()
{
  return stek_master_setter_running;
}

/*****************************************************************************
 * Overview of this module:   Session-Ticket-Encryption-Key (STEK) functionality
 *    which include generating, setting, getting of stek for the POD
 *    (redis connected network of nodes that share a common STEK).
 *    All aspects of code in this module run in the ats_ssl_session_reuse plugin
 *    execution space, (as opposed to redis_subscriber, or redis server execution space).
 *
 * Three main areas of interest in this module:
 *
 * 1) Initialization - STEK_init_keys()
 *
 * 2) STEK_Update_Setter_Thread() or  Master STEK setter - Ultimately there is one Master-STEK
 * setter node per POD where a POD is determined by all nodes on the redis network.
 * At regular intervals this thread will generate a new STEK and publish it to the POD.
 * This ensures proper key rotation.
 *
 * One node in the pod is configured to be Master STEK setter in charge of key rotations.
 * The algorithm implemented allows for the POD to dynamically self configure a master
 * which can recover from death of master, misconfigurations, ultimately ineffective rotations.
 *
 * General description:
 *
 * - Master thread (of ATS plugin)-
 * 	Create and send STEK
 * 	sleep for configured period (e.g. 7 hours)
 *	After wake up, if I'm no longer using STEK I created, then the POD is sync'd to a new
 *		POD stek-master...yield to it by exiting this thread.
 */

/*----------------------------------------------------------------------------------------*/

const char *
get_key_ptr()
{
  return channel_key;
}

int
get_key_length()
{
  return channel_key_length;
}

static int
STEK_GetGoodRandom(char *buffer, int size, int needGoodEntropy)
{
  FILE *fp;
  int numread = 0;
  char *randFileName;

  /* /dev/random blocks until good entropy and can take up to 2 seconds per byte on idle machines */
  /* /dev/urandom does not have entropy check, and is very quick.
   * Caller decides quality needed */
  randFileName = const_cast<char *>((needGoodEntropy) ? /* Good & slow */ "/dev/random" : /*Fast*/ "/dev/urandom");

  if (nullptr == (fp = fopen(randFileName, "r"))) {
    // printf("Can't open %s",randFileName);
    return 0; /* failure */
  }
  numread = static_cast<int>(fread(buffer, 1, size, fp));
  fclose(fp);

  return ((numread == size) ? 1 /* success*/ : 0 /*failure*/);

} /* STEK_GetRandom() */

static int
STEK_CreateNew(struct ssl_ticket_key_t *returnSTEK, int globalkey, int entropyEnsured)
{
  /* Create a new Session-Ticket-Encryption-Key and places it into buffer provided
   * return 0 on failure, 1 on success.
   * if boolean globalkey is set (inidcating it's the global key space),
     will get global key lock before setting */

  struct ssl_ticket_key_t newKey; // tmp local buffer

  /* We create key in local buff to minimize lock time on global,
   * because entropy ensuring can take a very long time e.g. 2 seconds per byte of entropy*/
  if ((!STEK_GetGoodRandom(reinterpret_cast<char *>(&(newKey.aes_key)), SSL_KEY_LEN, (entropyEnsured) ? 1 : 0)) ||
      (!STEK_GetGoodRandom(reinterpret_cast<char *>(&(newKey.hmac_secret)), SSL_KEY_LEN, (entropyEnsured) ? 1 : 0)) ||
      (!STEK_GetGoodRandom(reinterpret_cast<char *>(&(newKey.key_name)), SSL_KEY_LEN, 0))) {
    return 0; /* couldn't generate new STEK */
  }

  // we presume return buffer may be the global active STEK, so we get lock first before setting
  if (globalkey) {
    ssl_key_lock.lock();
  }
  memcpy(returnSTEK, &newKey, sizeof(struct ssl_ticket_key_t));
  if (globalkey) {
    ssl_key_lock.unlock();
  }

  memset(&newKey, 0, sizeof(struct ssl_ticket_key_t)); // keep our stack clean

  return 1; /* success */
}

static int
STEK_encrypt(struct ssl_ticket_key_t *stek, const char *key, int key_length, char *ret_encrypted, int *ret_len)
{
  /* Encrypted stek will be placed in caller allocated ret_encrypted buffer */
  /* NOTE: ret_len must initially contain the size of the ret_encrypted buffer */
  /* return 1 on success, 0 on failure  */
  int stek_len          = sizeof(struct ssl_ticket_key_t);
  size_t encrypted_size = *ret_len;
  size_t encrypted_len  = 0;
  int ret               = -1;

  if ((ret = encrypt_encode64(reinterpret_cast<const unsigned char *>(key), key_length, reinterpret_cast<unsigned char *>(stek),
                              stek_len, ret_encrypted, encrypted_size, &encrypted_len)) == 0) {
    *ret_len = encrypted_len;
  } else {
    TSDebug(PLUGIN, "STEK_encrypt calling encrypt_encode64 failed, error: %d", ret);
  }
  return ret;
}

static int
STEK_decrypt(const std::string &encrypted_data, const char *key, int key_length, struct ssl_ticket_key_t *ret_STEK)
{
  if (!ret_STEK) {
    return -1;
  }

  TSDebug(PLUGIN, "STEK_decrypt: requested to decrypt %lu bytes", encrypted_data.length());

  int ret                  = -1;
  size_t decrypted_size    = DECODED_LEN(encrypted_data.length()) + EVP_MAX_BLOCK_LENGTH * 2;
  size_t decrypted_len     = 0;
  unsigned char *decrypted = new unsigned char[decrypted_size];

  std::memset(decrypted, 0, decrypted_size);
  if ((ret = decrypt_decode64(reinterpret_cast<const unsigned char *>(key), key_length, encrypted_data.c_str(),
                              encrypted_data.length(), decrypted, decrypted_size, &decrypted_len)) != 0) {
    TSDebug(PLUGIN, "STEK_decrypt calling decrypt_decode64 failed, error: %d", ret);
    goto Cleanup;
  }

  if (sizeof(struct ssl_ticket_key_t) != decrypted_len) {
    TSError("STEK data length mismatch, got %lu, should be %lu", decrypted_len, sizeof(struct ssl_ticket_key_t));
    ret = -1;
    goto Cleanup;
  }

  memcpy(ret_STEK, decrypted, sizeof(struct ssl_ticket_key_t));
  memset(decrypted, 0, decrypted_size); // warm fuzzies
  ret = 0;

Cleanup:

  if (decrypted) {
    delete[] decrypted;
  }

  return ret; /* ok, length of data in ret_STEK will be sizeof(struct ssl_ticket_key_t) */
}

int
STEK_Send_To_Network(struct ssl_ticket_key_t *stekToSend)
{
  /* Send new STEK to redis network */
  /* This function encrypts the STEK and then sends it to the redis network. */
  /* Description: The subscriber thread listens on the redis network and updates its ATS
   * core with the new data
   */
  char encryptedData[STEK_MAX_ENC_SIZE] = {
    0,
  };
  int encryptedDataLength = 0;

  // encrypt the STEK before sending
  encryptedDataLength = sizeof(encryptedData);
  if (STEK_encrypt(stekToSend, get_key_ptr(), get_key_length(), encryptedData, &encryptedDataLength) != 0) {
    TSError("STEK_encrypt failed, not sending.");
    return 0; // failure
  }

  std::string redis_channel = ssl_param.cluster_name + "." + STEK_ID_NAME;
  ssl_param.pub->publish(redis_channel, encryptedData); // send it

  memset(encryptedData, 0, sizeof(encryptedData)); // warm fuzzies
  return 1;
}

static void *
STEK_Update_Setter_Thread(void *arg)
{
  plugin_threads.store(::pthread_self());
  ::pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, nullptr);
  ::pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, nullptr);

  int sleepInterval;
  struct ssl_ticket_key_t newKey;
  int startProblem = 0; // counter for start up and retry issues.

  /* This STEK-Master thread runs for the life of the executable setting the
   * Session-Ticket-Encryption-Key at every configured interval.
   * The key is generated and then redis published to the POD.
   * where the subscribers will pick it up and replace their ATS keys
   * If it detects another master in the POD setting keys, it will shut itself down */

  if (stek_master_setter_running) {
    /* Sanity check triggered.  Already running...don't do another. */
    TSDebug(PLUGIN, "Faulty STEK-master launch. Internal error. Moving on...");
    return nullptr;
  }

  stek_master_setter_running = true;
  TSDebug(PLUGIN, "Will now act as the STEK rotator for POD.");

  while (!plugin_threads.shutdown) {
    try {
      // Create new STEK, set it for me, and then send it to the POD.
      if ((!STEK_CreateNew(&newKey, 0, 1 /* entropy ensured */)) || (!STEK_Send_To_Network(&newKey))) {
        // Error occurred. We will retry after a short interval.
        // perhaps publishig isn't up yet.
        startProblem++;     // count how many times we have this problem.
        sleepInterval = 60; // short sleep for error retry
        TSError("Could not create/send new STEK for key rotation... Try again in %d seconds.", sleepInterval);
      } else {
        //  Everything good. will sleep for normal rotation time period and then repeat
        startProblem = 0;
        TSDebug(PLUGIN, "New POD STEK created and sent to network.");

        sleepInterval = ssl_param.key_update_interval;
      }

      ::sleep(sleepInterval);

      if ((!startProblem) && (memcmp(&newKey, &ssl_param.ticket_keys[0], sizeof(struct ssl_ticket_key_t)))) {
        /* I am not using the key I set before sleeping.  This means node (and POD) has
         * sync'd onto a more recent master,  which we will now yield to by exiting
         * out of this thread. */
        goto done_master_setter;
      }

      if (startProblem > 60) {
        /* We've been trying every minute for more than an hour. Time to give up and move on..*/
        /* Another node in pod will notice and pick up responsibility, else we'll try again later */
        goto done_master_setter;
      }
    } catch (...) {
      TSDebug(PLUGIN, "STEK_Update_Setter_Thread exception");
      goto done_master_setter;
    }
  } // while(forever)

done_master_setter:
  TSDebug(PLUGIN, "Yielding STEK-Master rotation responsibility to another node in POD.");
  memset(&newKey, 0, sizeof(struct ssl_ticket_key_t));
  stek_master_setter_running = false;
  return nullptr;
}

time_t lastChangeTime = 0;

void
STEK_update(const std::string &encrypted_stek)
{
  struct ssl_ticket_key_t newSTEK;
  if (STEK_decrypt(encrypted_stek, get_key_ptr(), get_key_length(), &newSTEK) == 0) {
    if (memcmp(&newSTEK, &ssl_param.ticket_keys[0], sizeof(struct ssl_ticket_key_t)) != 0) {
      /* ... and it's a new one,  so we will now set and use it. */
      ssl_key_lock.lock();
      memcpy(&ssl_param.ticket_keys[1], &ssl_param.ticket_keys[0], sizeof(struct ssl_ticket_key_t));
      memcpy(&ssl_param.ticket_keys[0], &newSTEK, sizeof(struct ssl_ticket_key_t));

      // Let TSAPI know about new ticket information
      stek_initialized = true;
      TSSslTicketKeyUpdate(reinterpret_cast<char *>(ssl_param.ticket_keys), sizeof(ssl_param.ticket_keys));
      time(&lastChangeTime);
      ssl_key_lock.unlock();
    }
  }
}

static void *
STEK_Update_Checker_Thread(void *arg)
{
  plugin_threads.store(::pthread_self());
  ::pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, nullptr);
  ::pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, nullptr);

  time_t currentTime;
  time_t lastWarningTime; // last time we put out a warning

  /* STEK_Update_Checker_Thread() - This thread runs forever, sleeping most
   * of the time, then checking and updating our Session-Ticket-Encryption-Key
   * If we don't get a STEK rotation during a certain time period,log warning
   * that something is up with our STEK master, and nominate a new STEK master.
   */

  TSDebug(PLUGIN, "Starting STEK_Update_Checker_Thread.");

  lastChangeTime = lastWarningTime = time(&currentTime); // init to current to suppress a startup warning.
  int check_count                  = 0;                  // Keep track of how many times we've checked whether we got a new STEK.

  while (!plugin_threads.shutdown) {
    try {
      if (!stek_initialized && ssl_param.pub) {
        // Launch a request for the master to resend you the ticket key
        std::string redis_channel = ssl_param.cluster_name + "." + STEK_ID_RESEND;
        ssl_param.pub->publish(redis_channel, ""); // send it
        TSDebug(PLUGIN, "Request for ticket.");
      }
      time(&currentTime);
      time_t sleepUntil;
      if (stek_initialized) {
        // Sleep until we are overdue for a key update
        sleepUntil       = 2 * STEK_MAX_LIFETIME - (currentTime - lastChangeTime);
        stek_initialized = false;
        check_count      = 0;
      } else {
        // Wait for a while in hopes that the server gets back to us
        sleepUntil = 30;
        ++check_count;
      }
      ::sleep(sleepUntil);

      if (check_count == 0) {
        continue;
      }

      /* We track last time STEK changed. If we haven't gotten a new STEK in twice the max,
       * then we figure something is wrong with the POD STEK master and nominate a new master.
       * STEK master may have been misconfigured, disconnected, died or who-knows.
       * If we're have been checking in the past five minutes and still haven't got a new
       * STEK, we believe that the master has died, so "now, I am the master".
       * ...no problem we will recover POD STEK rotation now */

      time(&currentTime);
      if ((currentTime - lastChangeTime) > (2 * STEK_MAX_LIFETIME) || check_count > 10) {
        // Yes we were way past due for a new STEK, and haven't received it.
        if ((currentTime - lastWarningTime) > STEK_NOT_CHANGED_WARNING_INTERVAL) {
          // Yes it's time to put another warning in log file.
          TSError("Session Ticket Encryption Key not syncd in past %d hours.",
                  static_cast<int>(((currentTime - lastChangeTime) / 3600)));

          lastWarningTime = currentTime;
        }

        /* Time to nominate a new stek master for pod key rotation... */
        if (!stek_master_setter_running) {
          TSDebug(PLUGIN, "Will nominate a new STEK-master thread now for pod key rotation.");
          TSThreadCreate(STEK_Update_Setter_Thread, nullptr);
        }
      }
    } catch (...) {
      TSDebug(PLUGIN, "STEK_Update_Checker_Thread exception");
      break;
    }
  } // while(forever)

  return nullptr;
} // STEK_Update_Checker_Thread()

int
STEK_init_keys()
{
  ssl_ticket_key_t initKey;

  channel_key_length = get_redis_auth_key(channel_key, MAX_REDIS_KEYSIZE);
  if (channel_key_length <= 0) {
    TSError("STEK_init_keys: Could not get redis authentication key.");
    return -1;
  }

  //  Initialize starter Session Ticket Encryption Key
  //  Will sync up with master later
  if (!STEK_CreateNew(&initKey, 0, 0 /*fast start*/)) {
    TSError("Can't init STEK.");
    return -1;
  }
  memcpy(&ssl_param.ticket_keys[0], &initKey, sizeof(struct ssl_ticket_key_t));
  memcpy(&ssl_param.ticket_keys[1], &initKey, sizeof(struct ssl_ticket_key_t));
  memset(&initKey, 0, sizeof(struct ssl_ticket_key_t));

  // Call into TSAPI to register the ticket info
  TSSslTicketKeyUpdate(reinterpret_cast<char *>(ssl_param.ticket_keys), sizeof(ssl_param.ticket_keys));

  stek_initialized = false;
  if (ssl_param.stek_master) {
    /* config has chosen this instance to initially launch the STEK setting master thread */
    /* We will generate and set STEK for the POD.  If things go weird, another node may
     * may take over this responsibility, so we only refer to this config parm at init. */
    TSThreadCreate(STEK_Update_Setter_Thread, nullptr);
    stek_initialized = true;
  } else {
    // Note that we have a temp key.  Probe the master once we are up
  }

  // Launch thread to listen for incoming STEK update rotation
  TSThreadCreate(STEK_Update_Checker_Thread, nullptr);

  return 1; // init ok
}
