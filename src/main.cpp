#include <Arduino.h>
/* SMS Manager for Power monitoring

  Uses GSM Library GPRS_Shield_Arduino.h

  Specifiche:
  - OK    Verifica corretta registrazione
  - OK    Reinizializzazione ogni 24 ore (ogni giorno) ad ora prestabilita
  - invio SMS mancanza energia fino a 3 numeri
  - invio SMS riattivazione energia fino a 3 numeri
  - disabilitazione o abilitazione notifica a numero da richiesta SMS
  - disabilitazione o abilitazione notifica a TUTTI i numeri da richiesta SMS
  - invio stato consumi da richiesta SMS a numero richiedente
  - set/reset pin uscita da SMS numero richiedente abilitato
  - stato pin ingresso su richiesta SMS a numero richiedente abilitato
  - OK    Salvataggio stato in memoria non volatile (EEPROM O FLASH)

  SoftwareSerial library Notes
  With Arduino 1.0 you should be able to use the SoftwareSerial library included with the distribution (instead of NewSoftSerial).
  However, you must be aware that the buffer reserved for incoming messages are hardcoded to 64 bytes in the library header,
  "SoftwareSerial.h": 1.define _SS_MAX_RX_BUFF 64 // RX buffer size
  This means that if the GPRS module responds with more data than that, you are likely to loose it with a buffer overflow!
  For instance, reading out an SMS from the module with "AT+CMGR=xx" (xx is the message index), you might not even see the message part because
  the preceding header information (like telephone number and time) takes up a lot of space.
  The fix seems to be to manually change _SS_MAX_RX_BUFF to a higher value (but reasonable so you don't use all you precious memory!)
  The Softwareserial library has the following limitations (taken from arduino page)
  If using multiple software serial ports, only one can receive data at a time.
  http://arduino.cc/hu/Reference/SoftwareSerial This means that if you try to add another serial device ie grove serial LCD you may get communication errors
  unless you craft your code carefully.

  AT+CLTS    Get Local Timestamp
  Test Command AT+CLTS=? Response +CLTS: "yy/MM/dd,hh:mm:ss+/-zz" OK
  (verificare differenza con AT+CCLK?)
  Read Command AT+CLTS?  Response +CLTS:<mode>	OK
  WriteCommand AT+CLTS=<mode>  Response OK	ERROR
  Mode 0 Disable		1 Enable

  Set and Save AT+CLTS=1;&W Response OK
  Restart the modem using Software Restart command AT+CFUN=1,1
  Response OK	RDY
  +CFUN: 1 +CPIN: READY
  Call Ready
  PSUTTZ: 2016,12,28,10,30,7,"+22",0 (unsolicited)
  DST: 0
  SMS Ready
  AT+CLTS? Response +CLTS 1
  OK (enabled)
  AT+CCLK? Response +CCLK: "16/12/28,16:01:27+22" OK

  Serial.println(F("String"));
*/

#include "GPRS_Shield_Arduino.h"
#include "EmonLib.h"
#include "EEPROM.h"

#define PIN_TX    2
#define PIN_RX    3
#define PIN_RST   7
#define BAUDRATE  9600
 /* M. Veneziano 2020 Voltage calibration Transformer + Partition. Set for each specific transformer */ 
#define VOLT_CAL 136.0 
#define MESSAGE_LENGTH 160
#define BUFFER_LENGTH 50
char message[MESSAGE_LENGTH]; // Message = 160 Char
char locDateTime[BUFFER_LENGTH];// Local Date and Time = 50 Char
/* Giorno, Ora e Minuto
Reset Day (rday) a 0 perchè diverso da 1 e da 31 e quindi 
consentirebbe il reset fin dalla prima ora giusta del primo giorno */
int day, rday=0, hh, mm;
//Ora e Minuto di Reset giornaliero
int ORAr = 03, MINr = 00;
int size,nphone;
String EStr;


int messageIndex = 0;
float PowerVoltage;
char phone[16], Autphone[16], phoneT[16];
char datetime[24];
//char *phoneAut[] = {"+393334188263","+393383418818", "+393391255597",""};
//char *phoneAut[] = {"+393460607220","+393383418818", "",""};
//char *phoneAut[] = {"+393460607220","", "",""};
char *phoneAut[] = {"+393334188263","","",""};
// phoneAut[0] authorized master number - Can athorize up to 3 phone numbers


char outmessage[50];
//char testo[50];
uint32_t iniTime, passTime;	// Valore Tempo iniziale, Valore tempo trascorso
uint32_t previousMilliscc = 0, previousMillisora = 0;
uint32_t intervalcc = 10000; //intervallo per il controllo del valore di tensione attuale - 10 sec
uint32_t intervalora = 900000; //intervallo per il controllo dell'ora - 15 Minuti - 3600000 1 ora
//uint32_t intervalora = 120000; //intervallo per il controllo dell'ora - 2 Minuti - 3600000 1 ora

//COOP INFO SMS (Credito residuo)
#define INFO_NUMBER "4243688"
#define INFOTXT  "SALDO"
//#define INFOTXT "INFO SIM"

//void writeString(int offs,String edata);
//String read_String(int offs);
//, tReadStr;

void writeString(int offs,String edata)
{
  int _size = edata.length();
  int i;
  for(i=0;i<_size;i++)
  {
    EEPROM.write(offs+i,edata[i]);
  }
  EEPROM.write(offs+_size,'\0');   //Add termination null character for String Data
}

String read_String(int offs)
{
  //int i;
  char edata[20]; //Max 20 Bytes
  int len=0;
  unsigned char k;
  k=EEPROM.read(offs);
  while(k != '\0' && len<20)   //Read until null character
  {    
    k=EEPROM.read(offs+len);
    edata[len]=k;
    len++;
  }
  edata[len]='\0';
  return String(edata);
}

GPRS gprs(PIN_TX, PIN_RX, BAUDRATE); //RX,TX,BaudRate
EnergyMonitor emon1;	//Initialize EnergyMonitor ?

void calc() {
  emon1.calcVI(20,2000);  // Calculate Emoncms parameters - Measurement on 20 halfwave with a 2000ms Timeout
  PowerVoltage   = emon1.Vrms;  //extract Vrms into Variable
  //irms[0] = emon1.calcIrms(4000);  // Calculate Irms only
  //irms[1] = irms[0] * 225.0;
}

void initgsm() {
  //Initialize Modem and emoncms

  Serial.begin(9600);
  //Serial.println("Program started...\nStarting Power On sequence");
  Serial.print(F("Program started...\nStarting Power On sequence\n"));

  
  emon1.voltage(0, VOLT_CAL, 1.7);  // Defines Voltage: input pin, Voltage calibration, phase_shift
  //emon1.current(0, 32);
  for (int i = 0; i < 5; i++) { //clean up data
    emon1.calcVI(20,2000); // Run 20 measurement made of 20 halfwave with a 2000ms Timeout
  }

  //  poweron();	//Initialize Modem
  gprs.powerUpDown(PIN_RST); //PIN_RESET (7) - RESET Modem
  delay(5000);

  //  gprs.checkPowerUp(); // Verifica se ritorna AT - Duplicato ?
  //  Serial.println("Verificato AT e regitrazione");
  /* Check "AT" - "OK"
    Check"AT+CFUN=1" - "OK"
    Set SMS to text mode
    Set message mode to ASCII Text Mode "AT+CMGF=1" - "OK"
    Set New Message Indicator "AT+CNMI=1,1,0,0,0" - "OK"
  */

   //Serial.println(" - Start GSM Modem Reset");
   
  while (!gprs.init()) {
    //Serial.print(" - init error\r\n");
    gprs.powerUpDown(PIN_RST); //PIN_RESET (7) - RESET Modem
    delay(1000);
  }
  delay(1000);
 
  Serial.print(F(" - Init Success - Completed GSM Power On Sequence - Reset\n"));
  iniTime = millis(); // Valore Tempo iniziale
  
// Garantisce che sia registrato sulla rete
  while (!gprs.isNetworkRegistered()) {
    delay(1000);
    Serial.print(F("Network has not registered yet!\n"));
    
}
  Serial.print(F("GSM network initialization done!\n"));

  delay(500);

  //Serial.println("DELETE ALL SMS UNREAD");
  
  messageIndex = gprs.isSMSunread();
    delay(2000);
  for (int i = messageIndex; i > 0; i--)
    {
    gprs.readSMS(i, message, MESSAGE_LENGTH, phone, datetime);
    delay(2000);

    //In order not to full SIM Memory, is better to delete it
    gprs.deleteSMS(i);
        //Serial.print("DELETED SMS n. ");
        //Serial.println(i);
        delay(2000);
    } 
    
    messageIndex = gprs.isSMSunread();
    delay(1000);
    Serial.print(F("messageIndex dopo cancellazione SMS: "));
    Serial.flush();
    Serial.println(messageIndex);
    Serial.flush();

    delay(1000);

  // Invia SMS al numero Coop Voce 42 43 688 INFO SIM per credito residuo
  // in modo da ricavare la data e l'ora corrente
  Serial.print(F("Invio Messaggio INFO\n"));
  //gprs.sendSMS(INFO_NUMBER, INFOTXT);

  if (gprs.sendSMS(INFO_NUMBER, INFOTXT)) { //define phone number and text
    Serial.print(F("Send SMS Succeed!\r\n"));
    Serial.flush();
  } 
else {
    Serial.print(F("Send SMS failed!\r\n"));
    Serial.flush();
  }
   
  messageIndex = gprs.isSMSunread();
  delay(500);
  Serial.print(F("N messaggi SMS in coda - messageIndex: "));
  Serial.flush();
  Serial.println(messageIndex);
  Serial.flush();
  delay(500);

  while (messageIndex == 0) {
    Serial.print("dopo SMS - messageIndex: ");
    Serial.flush();
    Serial.println(messageIndex);
    Serial.flush();
    messageIndex = gprs.isSMSunread();
    delay(500);
  }

  // delay (2000);
    Serial.print(F("Legge i messaggi INFO ricevuti"));
  
    //There are 2 UNREAD SMS following INFO SIM SMS
    //gprs.readSMS(2, message, MESSAGE_LENGTH, phone, datetime);
    //In order not to full SIM Memory, is better to delete it
    //gprs.deleteSMS(2);
    //delay (2000);
    gprs.readSMS(1, message, MESSAGE_LENGTH, phone, datetime);
    //In order not to full SIM Memory, is better to delete it
    gprs.deleteSMS(1);
    delay (2000);

    messageIndex = gprs.isSMSunread();
    delay(2000);
  for (int i = messageIndex; i > 0; i--)
    {
    gprs.readSMS(i, message, MESSAGE_LENGTH, phone, datetime);
    delay(2000);
    //In order not to full SIM Memory, is better to delete it
    gprs.deleteSMS(i);
        //Serial.print("DELETED SMS n. ");
        //Serial.println(i);
        delay(2000);
    } 

    messageIndex = gprs.isSMSunread();
    delay(500);
    Serial.print(F("N messaggi SMS rimanenti - dopo SMS INFO: "));
    Serial.flush();
    Serial.println(messageIndex);
    Serial.flush();
    delay(2000);

    Serial.print("From number: ");
    Serial.println(phone);
    Serial.flush();
    Serial.print("Datetime: ");
    Serial.println(datetime);
    Serial.flush();
    Serial.print("Received Message: ");
    Serial.println(message);
    Serial.flush();

    sim900_check_with_cmd(F("AT+CLTS=0\r\n"), "OK", CMD);
    sprintf (outmessage, "AT+CCLK=\"%s\"\r\n", datetime);
    Serial.println (outmessage);
    Serial.flush();
    if (sim900_check_with_cmd (outmessage, "OK", CMD)){
      Serial.println ("A buon Fine");
      Serial.flush();
    }
      else {
    Serial.println ("Non a buon Fine");
    Serial.flush();
    } 

   //AT+CLTS=1	// Enable date and time from network - WIND Funziona COOP VOCE NON FUNZIONA
  
  // sim900_check_with_cmd(F("AT+CLTS=1\r\n"), "OK", CMD);
  // delay(2000);

  // Sync date time GSM RTC to Network
  //sim900_check_with_cmd(F("AT+CCLK?\r\n"), "OK", 0);
  //	AT+CCLK?						--> 8 + CR = 9
  //+CCLK: "14/11/13,21:14:41+04"	--> CRLF + 29 + CRLF = 33
  //
  //OK							--> CRLF + 2 + CRLF =  6
  /*
    "yy/MM/dd,hh:mm:ss+/-zz"
    zz quarter (-47....+48)of hour between local time and GMT
    6 maggio 2010,00:01:52 GMT +2 ore
    "10/05/06,00:01:52+08
  */
  gprs.getDateTime(locDateTime);
  Serial.print(" Data e Ora da rete: ");
  Serial.println(locDateTime);
  //AT+CLTS=0  // DISable date and time from network - Only once, later the internal RTC should remember it
  //sim900_check_with_cmd(F("AT+CLTS=0\r\n"), "OK", CMD);
  //delay(1000);

  //AT+CMGF=1	// Enable ASCII TEXT mode for SMS
  if (sim900_check_with_cmd(F("AT+CMGF=1\r\n"), "OK", CMD)) { // Set message mode to ASCII
      Serial.println(" Set ASCII TEXT mode for SMS....");
  }

   delay(500);

  
}

bool TimeToReset () {
  // Reset Now?

  //String tmp;
  char BuffDateTime[30];
  //MARCO EDIT   char hh[2];
  //MARCO EDIT   char mm[2];
  // int day, rday, hh, mm;
  gprs.getDateTime(BuffDateTime);
  //delay(500);
  //Serial.print(" TimeToReset Data e Ora: ");
  //Serial.println(BuffDateTime);
  //tmp = String(BuffDateTime);
  /*MARCO EDIT   hh[0] = BuffDateTime[9];
    hh[1] = BuffDateTime[10];
    mm[0] = BuffDateTime[12];
    mm[1] = BuffDateTime[13];
    MARCO EDIT */
  day = (BuffDateTime[0] - '0') * 10 + (BuffDateTime[1] - '0');
  hh = (BuffDateTime[9] - '0') * 10 + (BuffDateTime[10] - '0');
  mm = (BuffDateTime[12] - '0') * 10 + (BuffDateTime[13] - '0');

  //hh=tmp.substring(13, 15);
  //mm=tmp.substring(16, 18);
  //(tmp.substring(13, 15)).toCharArray(hh, 2);
  //(tmp.substring(16, 18)).toCharArray(mm, 2);

  //Serial.println(" TimeToReset Ora e minuti -->");
  //.println(hh);
  //delay (1000);
  //Serial.println(" TimeToReset Minuti");
  //Serial.println(mm);
  //Serial.println("...");

  Serial.print("ORA, MIN --> ");
  Serial.print(hh);
  Serial.print(":");
  Serial.print(mm);
  Serial.println(" <--");
  if (hh == ORAr && (day != rday)) {
    rday = day;
    return true;
  }
  return false;
}


void setup() {
  // analogReference(DEFAULT);
  pinMode(PIN_RST, OUTPUT);
  initgsm(); // Inizializza GSM e Valore Tempo iniziale
  EEPROM.update(5, 0); // Aggiorna EEPROM 5 a 0 solo se non è già a 0

// Load in EEPROM predefined phone numbers
  for (int i = 0; i < 4; i++) {
      EStr = read_String(6+i*17);

    // If in EEPROM the first char is '+' it is considered that a phone is loaded
    // otherwise load the predefined number if defined in FLASH (phoneAut).
    // Used to save EEPROM writing
    //Serial.print (strlen(phoneAut[i]));
    //Serial.print (EStr[0]);
    //Serial.print (EStr);

      if ((EStr[0] != '+') && (strlen(phoneAut[i]) == 0)) {
        nphone=i;
        break;
      }

      if((EStr[0] != '+') && (strlen(phoneAut[i]) > 0)) {
        writeString((6+i*17), phoneAut[i]);  //Initial Address 6 and String type data [16 char])
        //EStr = read_String(6+i*17); crea problemi anche se non viene eseguita. Mah?!
        EStr=phoneAut[i];
        }

      Serial.print ("EEPROM autorized phone n.");
      //delay(100);
      Serial.print (i);
      Serial.print (": ");
      Serial.println (EStr);
      
   } 

  Serial.print ("Numero di telefoni: ");
  Serial.println(nphone);
}

void loop() {
/*
  messageIndex = gprs.isSMSunread();
  delay(1000);
  Serial.print(F("N messaggi SMS in coda - LOOPx: "));
  Serial.flush();
  Serial.println(messageIndex);
  Serial.flush();
  delay(2000);

  messageIndex = gprs.isSMSunread();
  if (messageIndex > 0) { 
    //At least, there is one UNREAD SMS
    gprs.readSMS(messageIndex, message, MESSAGE_LENGTH, phone, datetime);
    Serial.print("At least, there is one UNREAD SMS");
    //In order not to full SIM Memory, is better to delete it
    gprs.deleteSMS(messageIndex);

    Serial.print("From number: ");
    Serial.println(phone);
    Serial.print("Datetime: ");
    Serial.println(datetime);
    Serial.print("Received Message: ");
    Serial.println(message);

    String phoneS = String(phone);
    String AutphoneS = String(phoneAut[0]);
    String messageS = String(message);

  if (phoneS == AutphoneS) { 
            
      if (messageS.substring(0,1) == "A"){ 
          messageS.substring(2).toCharArray(phoneT, 16);
          phoneAut[1] = phoneT;
            }
      if (messageS.substring(0,1) == "D"){ 
        // Delete in EEPROM predefined aux phone numbers
        for (int i = 1; i < 4; i++) {
            writeString((6+i*17), "");  //Initial Address 6 and String type data [16 char])
            Serial.print("Delete all numbers");
            }        
        }
  }
    } 
*/

/*
  */
  //  AT+CLTS=1	// Enable date and time from network

  //	AT+CCLK?						--> 8 + CR = 9
  //+CCLK: "14/11/13,21:14:41+04"	--> CRLF + 29+ CRLF = 33
  //
  //OK							--> CRLF + 2 + CRLF =  6
  /*
    "yy/MM/dd,hh:mm:ss+/-zz"
    zz quarter (-47....+48)of hour between local time and GMT
    6 maggio 2010,00:01:52 GMT +2 ore
    "10/05/06,00:01:52+08
  */
  // passTime = millis() - iniTime; // Tempo trascorso da inizializzazione ?
  //if ((millis() - passTime) > 86400000) {
    //	  powerDown();
    /*
      Restart the modem using Software Restart command AT+CFUN=1,1
      or you can hardware restart it.
      AT+CFUN=1,1
    */
    //	  gprs.powerReset(PIN_RESET); //PIN_RESET (7) - Si resetta veramente il Modem GSM?
    //	  init(); // Re-Inizializza - Si res-inizializzazione veramente il Modem GSM?
  //}


  unsigned long currentMillis = millis();
  // Verifica ogni 5 secondi (intervalcc)
  if (currentMillis - previousMilliscc > intervalcc) {
      previousMilliscc = currentMillis;
  
      messageIndex = gprs.isSMSunread();
      if (messageIndex > 0) { 
        //At least, there is one UNREAD SMS
        gprs.readSMS(messageIndex, message, MESSAGE_LENGTH, phone, datetime);
        Serial.print("At least, there is one UNREAD SMS");
        //In order not to full SIM Memory, is better to delete it
        gprs.deleteSMS(messageIndex);
    
        Serial.print("From number: ");
        Serial.println(phone);
        Serial.print("Datetime: ");
        Serial.println(datetime);
        Serial.print("Received Message: ");
        Serial.println(message);
    
        String phoneS = String(phone);
        String AutphoneS = String(phoneAut[0]);
        String messageS = String(message);
    
      if (phoneS == AutphoneS) { 
                
          if (messageS.substring(0,1) == "A"){ 
              messageS.substring(2).toCharArray(phoneT, 16);
              phoneAut[1] = phoneT;
                }
          if (messageS.substring(0,1) == "D"){ 
            // Delete in EEPROM predefined aux phone numbers
            for (int i = 1; i < 4; i++) {
                writeString((6+i*17), "");  //Initial Address 6 and String type data [16 char])
                Serial.print("Delete all numbers");
                }        
            }
      }
        } 

      calc();				//	Calculates PowerVoltage Vrms
      Serial.print(" Current Voltage: ");
      Serial.flush();
      Serial.println(PowerVoltage);
      Serial.flush();
	  
    if (PowerVoltage <= 180.0) {

      // MANCANZA ALIMENTAZIONE
      // EEPROM.read(5)  0 Alimentazione presente 1 Alimentazione assente
        /* Serial.println("MANCANZA ALIMENTAZIONE");
        int ER = (int) EEPROM.read(5);
        Serial.println(ER);
        */
        // Identifica transizione da Alimentazione Presente a Alimentazione Assente
        if (EEPROM.read(5) == 0) {
            EEPROM.update(5, 1);  // Aggiorna EEPROM a 1 solo se non è già a 1
            Serial.println("INVIO SMS PWR OFF");
			      int power = (int)(PowerVoltage);

            gprs.getDateTime(locDateTime);
            Serial.print(" Data e Ora da rete: ");
            Serial.println(locDateTime);

			      sprintf(outmessage, "%s %s %d Vac", locDateTime,"MANCANZA ALIMENTAZIONE, ultima lettura:", power);
			      Serial.println(outmessage);

            // Riconoscendo la transizione ON->OFF invia SMS a Numero/i telefono autorizzati
            for (int i = 0; i < nphone; i++){
            if (gprs.sendSMS(phoneAut[i], outmessage)) { 
                  Serial.print("Send SMS Succeed!\r\n");
		          } else {
                     Serial.print("Send SMS failed!\r\n");
			              }
            }
		      }

    }
    // ALIMENTAZIONE PRESENTE
    else {
         /* Serial.println("ALIMENTAZIONE PRESENTE");
         int ER = (int) EEPROM.read(5);
         Serial.println(ER);
         */
         if (EEPROM.read(5) == 1){
             // Identifica transizione da Alimentazione assente ad Alimentazione a presente
             EEPROM.update(5, 0); // Aggiorna EEPROM a 0 solo se non è già a 0
             Serial.println("INVIO SMS POWER ON");
             int power = (int)(PowerVoltage);
             gprs.getDateTime(locDateTime);
             Serial.print(" Data e Ora da rete: ");
             Serial.println(locDateTime);

             sprintf(outmessage, "%s %s %d Vac", locDateTime, "RIPRESA ALIMENTAZIONE, ultima lettura:", power);
              Serial.println(outmessage);

            // Riconoscendo la transizione OFF->ON invia SMS a Numero/i telefono autorizzati
            for (int i = 0; i < nphone; i++){
                if (gprs.sendSMS(phoneAut[i], outmessage)) { 
                  Serial.print("Send SMS Succeed!\r\n");
		            } else {
                     Serial.print("Send SMS failed!\r\n");
			              }
            }
          }
    }

	  }

/* RESET GIORNALIERO 
Gestisce l'evento di avvenuto reset del GSM controllando giorno e l'ora
(il controllo viene effettuato ogni 15 Minuti) */
  if (currentMillis - previousMillisora > intervalora) {
    previousMillisora = currentMillis;
    gprs.getDateTime(locDateTime);
    Serial.print("Verifica ogni 15 Min del RESET Data e Ora: ");
    Serial.println(locDateTime);
    Serial.println("Ora chiama TimeToReset per verificare se è il momento di resettare");
	
    if (TimeToReset() == true)  {
	    Serial.print (" Devo fare Reset ");
      initgsm();
    }
    }

}