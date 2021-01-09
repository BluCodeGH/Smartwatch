#include "ntp.h"
#include "lwip/sockets.h"
#include <time.h>
#include <sys/time.h>

const int NTP_PACKET_SIZE = 48; // NTP time is in the first 48 bytes of message
const int NTP_TIMEOUT = 1500;
const int NTP_PORT = 123;

time_t decodeNTPMessage(char *messageBuffer) {
  unsigned long secsSince1900;
  // convert four bytes starting at location 40 to a long integer
  secsSince1900 = (unsigned long)messageBuffer[40] << 24;
  secsSince1900 |= (unsigned long)messageBuffer[41] << 16;
  secsSince1900 |= (unsigned long)messageBuffer[42] << 8;
  secsSince1900 |= (unsigned long)messageBuffer[43];

  time_t time = secsSince1900 - 2208988800UL; //Compensate for different epochs (1900 vs 1970)
  return time;
}

ntpClass::ntpClass() {
}

int ntpClass::sock;

sockaddr_in setup_socket(char* server) {
  struct sockaddr_in dest_addr;
  dest_addr.sin_addr.s_addr = inet_addr(server);
  dest_addr.sin_family = AF_INET;
  dest_addr.sin_port = htons(NTP_PORT);
  ntpClass::sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
  if (ntpClass::sock < 0) {
    printf("Unable to create socket: errno %d\n", errno);
  }
  timeval timeout;
  timeout.tv_sec = 0;
  timeout.tv_usec = 500000;
  ESP_ERROR_CHECK(setsockopt(ntpClass::sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)));
  return dest_addr;
}

bool sendNTPpacket(sockaddr_in dest_addr) {
  uint8_t ntpPacketBuffer[NTP_PACKET_SIZE]; //Buffer to store request message
  // set all bytes in the buffer to 0
  memset(ntpPacketBuffer, 0, NTP_PACKET_SIZE);
  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  ntpPacketBuffer[0] = 0b11100011;   // LI, Version, Mode
  ntpPacketBuffer[1] = 0;     // Stratum, or type of clock
  ntpPacketBuffer[2] = 6;     // Polling Interval
  ntpPacketBuffer[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  ntpPacketBuffer[12] = 49;
  ntpPacketBuffer[13] = 0x4E;
  ntpPacketBuffer[14] = 49;
  ntpPacketBuffer[15] = 52;
  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:
  int err = sendto(ntpClass::sock, ntpPacketBuffer, NTP_PACKET_SIZE, 0, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
  if (err < 0) {
    printf("Error occurred during sending: errno %d\n", errno);
    return false;
  }
  printf("Message sent\n");
  return true;
}

unsigned long millis() {
  return (unsigned long) (esp_timer_get_time() / 1000ULL);
}

time_t ntpClass::getTime(char *server) {
  printf("Starting UDP\n");
  sockaddr_in dest_addr = setup_socket(server);

  sendNTPpacket(dest_addr);
  char ntpPacketBuffer[NTP_PACKET_SIZE]; //Buffer to store response message
  uint32_t beginWait = millis();
  while (millis() - beginWait < NTP_TIMEOUT) {
    printf("Receive NTP Response\n");
    struct sockaddr_in source_addr; // Large enough for both IPv4 or IPv6
    socklen_t socklen = sizeof(source_addr);
    int len = recvfrom(sock, ntpPacketBuffer, NTP_PACKET_SIZE, 0, (struct sockaddr *)&source_addr, &socklen);
    // Error occurred during receiving
    if (len < 0) {
        printf("recvfrom failed: errno %d\n", errno);
        break;
    }

    time_t timeValue = decodeNTPMessage(ntpPacketBuffer);
    struct timeval now;
    now.tv_sec=timeValue;
    now.tv_usec=0;
    settimeofday(&now, NULL);

    shutdown(sock, 0);
    close(sock);
    printf("Successful NTP sync.\n");
    return timeValue;
  }
  printf("No NTP Response.\n");
  shutdown(sock, 0);
  close(sock);
  return 0; // return 0 if unable to get the time
}

time_t ntpClass::getTime(const char* server) {
  return ntpClass::getTime((char*)server);
}

ntpClass ntp;
