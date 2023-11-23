/////////////////////////////////////////////////////////////////////////// Custom Code
int isCodec8(int ,unsigned char*);
void revmemcpy (void*, const void*, size_t);


struct atrack_wir_message {
	char message[200];
	char asciiAux[20];
	char gpsQuality;
	int bufferScanIndex;
	uint64_t id;
	uint16_t idMapIndex;
	uint64_t gpsDateTime;
	int32_t longitude;
	int32_t latitude;
	uint16_t heading;
	uint16_t speed;
	uint8_t event;
	uint32_t odometer;
	int16_t temperature1;
	int16_t temperature2;
	int16_t temperature3;
};

struct mapIdToName {
    uint64_t id;
    uint64_t port;
    char* name;
};

#define deviceCount 40
struct mapIdToName nameMap[deviceCount] = {
	{ 350612075727717,0, "3862BZB"},// PLMR
	{ 350612075725976,0, "3854YAE"},
	{ 350612075878015,0, "1174KTU"},
	{ 350612075825909,0, "2560YUE"},
	{ 350612075825669,0, "1174KSR"},
	{ 350612075727949,0, "854HRL"},
	{ 350612075727873,0, "1295RDP"},
	{ 350612075878163,0, "1174KYG"},
	{ 350612075877942,0, "3854YDR"},
	{ 350612075831360,0, "1122LLA"},
	{ 350612075727857,0, "2601NYU"},
	{ 350612075727774,0, "3858EHB"},
	{ 350612075727766,0, "2601PBE"},
	{ 350612075727931,0, "3854YCL"},
	{ 350612075727865,0, "2569ULK"},
	{ 350612075725836,0, "2566HFC"},
	{ 350612075727790,0, "2566XFP"},
	{ 350612075864965,0, "3862CGB"},
	{ 350612075865137,0, "2350ZGX"},
	{ 350612075877793,0, "068KSK"},
	{ 350612075877991,0, "3858EEN"},
	{ 350612075726032,0, "2899TRT"},
	{ 350612075877868,0, "2569UDH"},
	{ 350612075877785,0, "3862CBF"},
	{ 350612075865194,0, "3862CFX"},
	{ 350612075865038,0, "1122LND"},
	{ 350612075825834,0, "854HSR"},
	{ 350612075908309,0, "3854YBH"},
	{ 350612075878247,0, "2569UEL"},
	{ 350612075865095,0, "2560YXH"},
	{ 350612075727956,0, "854HPH"},
	{ 350612075727725,0, "1122LRK"},
	{ 350612075877959,0, "1174KUA"},
	{ 350612075908291,0, "1109ZBR"},
    { 357073291703367,0, "2513KNR"}, // BLV EXP
	{ 357073294170614,0, "3004TZI"},
	{ 357073294152570,0, "3004UAK"},
	{ 357073294151937,0, "3049RCH"},
	{ 357073294152489,0, "3164UHN"},
	{ 357073294152034,0, "3164UIS"},
};

struct eventIdToName {
    uint64_t id;
    char* name;
};

#define eventCount 11
const struct eventIdToName eventMap[eventCount] = {
	{0, "Rastreo por solicitud"}, // "Rastreo por solicitud"
	{2, "tracker"}, // "Rastreo por tiempo"
	{4, "Rastreo por distancia"}, // "Rastreo por distancia"
	{5, "Rastreo por cambio de rumbo"},  // "Rastreo por cambio de rumbo"
	{101, "acc on"}, // "Contacto Encendido"
	{102, "ac alarm"}, // "Batería Desconectada"
	{113, "acc off"}, // "Contacto Apagado"
	{115, "Batería Reconectada"}, // "Batería Reconectada"
	{109, "sensor alarm"}, // "Frenado bruzco"
	{110, "Aceleración bruzca"}, // "Aceleración bruzca"
	{111, "Curva Bruzca"}, // "Curva Bruzca"
};

void revmemcpy (void *dest, const void *src, size_t len)
{
  char *d = dest + len - 1;
  const char *s = src;
  while (len--)
    *d-- = *s++;
  //return
}

int isCodec8(int buflen,unsigned char* buffer){
  uint32_t aux = 0;

  if (buffer[0] || buffer[1] || buffer[2] || buffer[3]) // check preamble
    return 0; 

  for (int i = 0; i < 4; i++){ // Load Codec8 "Data Field Length" value to Aux (4 bytes)
    aux *= 256; // shift one byte left
    aux += buffer[i + 4];
  }

  // Check for correct "Data Field Length", "Codec ID", and matching "Number of Data 1 and Number of Data 2 Values"
  if ((buflen == aux + 12) && (buffer[8] == 0x08) && (buffer[9] == buffer[buflen - 5]))
    return 1;

  return 0;
}

///////////////////////////////////////////////////////////////////////////