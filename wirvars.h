/////////////////////////////////////////////////////////////////////////// Custom Code

struct atrack_wir_message {
	char message[200];
	char asciiAux[20];
	char gpsQuality;
	int bufferScanIndex;
	uint64_t id;
	uint16_t idMapIndex;
	uint32_t gpsDateTime;
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

#define deviceCount 49
struct mapIdToName nameMap[deviceCount+1] = {
	{ 860041049947370,0, "492SBL"}, // PLMR
	{ 860041049948378,0, "3862BZB"},
	{ 860041049948618,0, "3854YAE"},
	{ 860041049948659,0, "1174KTU"},
	{ 860041049948972,0, "2560YUE"},
	{ 860041049950069,0, "1174KSR"},
	{ 860041049950226,0, "854HRL"},
	{ 860041049952586,0, "1295RDP"},
	{ 860041049952685,0, "1174KYG"},
	{ 864893036193848,0, "2870ZCA"},
	{ 864893036194507,0, "3796LIL"},
	{ 865374039025377,0, "3854YDR"},
	{ 865374039028603,0, "1122LLA"},
	{ 865374039029841,0, "2601NYU"},
	{ 865374039030401,0, "3858EHB"},
	{ 865374039030831,0, "2601PBE"},
	{ 865374039034262,0, "3854YCL"},
	{ 865374039043115,0, "1122LKU"},
	{ 865374039046993,0, "2569ULK"},
	{ 865374039047439,0, "2566HFC"},
	{ 865374039048262,0, "492RZG"},
	{ 865374039048676,0, "2566HGF"},
	{ 865374039049641,0, "854HYG"},
	{ 865374039050375,0, "1122LPG"},
	{ 865374039059285,0, "2566XFP"},
	{ 865374039059632,0, "3862CGB"},
	{ 865374039061471,0, "2350ZGX"},
	{ 869139050365317,0, "1180PKB"},
	{ 869139050365341,0, "068KSK"},
	{ 869139050365408,0, "3858EEN"},
	{ 869139050365895,0, "2899TRT"},
	{ 869139050368154,0, "2569UDH"},
	{ 869139050368295,0, "3862CBF"},
	{ 869139050368436,0, "3862CFX"},
	{ 869139050370895,0, "1122LND"},
	{ 869139050371026,0, "854HSR"},
	{ 869139050376777,0, "3854YBH"},
	{ 869139050376942,0, "2569UEL"},
	{ 869139050377148,0, "2560YXH"},
	{ 869139050378096,0, "854HPH"},
	{ 869139050378476,0, "1122LRK"},
	{ 869139050378591,0, "1174KUA"},
	{ 869139050378716,0, "1109ZBR"},
    { 864895030722027,0, "2513KNR"}, // BLV EXP
	{ 864895030722373,0, "3004TZI"},
	{ 864895030897720,0, "3004UAK"},
	{ 864895030897837,0, "3049RCH"},
	{ 864895030897746,0, "3164UHN"},
	{ 864895030897506,0, "3164UIS"},
	{ 0,0, "UNKNWN"}
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

///////////////////////////////////////////////////////////////////////////