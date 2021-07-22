// verzie
//
// 2015-08-01 nova verzia
// 2016-02-27 ked sa vycerpa voda musi sa zavlaha vypnut
// 2016-03-28 analogove hranice na kontrolu hladin v studni zmenene zo 700 na 800, pri merani bolo okolo 730-740 ked bola voda na kontaktoch
// 2016-04-10 zmena nastaveni PRAZDNA, casy spustenia, delay na 120
// 2016-05-02 hranice senzorov v studni na 800
// 2016-05-08 ak ide zavlaha nedolievaj vodu do nadrze aby vlny nerusili snimac hladiny
// 2016-05-15 doplnene nastavenie casu, moznost zastavenia a spustenia zavlahy tlacitkom
// 2016-05-18 oprava chyby ze pri vypinani zavlahy tlacitkom neboli realne vypnute rele
// 2016-06-23 pri chybe sa najskor prepise display kym sa aktivuje nekonecna slucka
// 2016-06-29 kvapkova zavlaha minimalizovana, aby polievala najviac prva sekcia
// 2016-07-02 prerobenie zadavania precent vycerpania a casov postreku nadoby pre kazdu sekciu, zlepsenie vypisu hladin pri ERR1
// 2017-04-13 znizenie hranice pri jeVodaNaMin a jeVodaNaMax na 750 koli castym ERR1, tie hodnoty chvilu skacu aj okolo 850 ked tam neni voda
// 2017-05-30 odstranena chyba vyratavania percent - desatinna bodka, vypis percent predpolievanim, aktivovana 6 sekcia
// 2017-06-04 odstranena chyba ze pri automatickom spusteni zavlahy chybalo realne spustenie ventilov
// 2018-05-27 odstranena chyba cvakanie rele cerpadla studne ked sa nadoba blizi k naplnenej a v studni je dost vody,
//            premenovanie funkcie setTime() na nastavCasPriSpustani()
// 2019-04-14 odpajanie GND kontaktu senzorov vlhkosti v studni
// 2020-06-07 include timelib.h namiesto time.h koli chybe v appke, VODA_PRAH na 1000
// 2020-07-05 voda prah 900, interval na novu skusku hladin 30 minut
// 2021-04-11 nastavenie modu polievania (vobec, vecer, rano aj vecer) pri zapinani

#define VERSION 20200705

#include <TimeLib.h>
#include <LiquidCrystal.h>

#define ECHOPIN 7 // Echo Pin
#define TRIGPIN 6 // Trigger Pin
#define PLNA 4 // vzdialenost v cm od senzora ked je nadrz plna
#define PRAZDNA 89 // vzdialenost v cm od senzora ked je nadrz prazdna
#define HLADINA_AKTIVACIE 95 // percento naplnenia nadrze pri ktorom zase zacne spinat cerpadlo a doplnat nadrz
#define VODA_PRAH 900 // hodnota hladinovaho senzora, prah ked detekuje vodu
#define CAS_STUDNA 1800 // cas v sekundach od posledneho cerpania zo studne

// technicke definicie
#define TIME_MSG_LEN  11   // time sync to PC is HEADER and unix time_t as ten ascii digits
#define TIME_HEADER  255   // Header tag for serial time sync message

// chyby
#define ERROR_MAX_MIN 1 // chyba: voda je detekovana na maxime ale neni detekovana na minime

// warningy
#define WRN_VECER_NEDOST_NADOBA -1 // varovanie: mala sa spustit vecer zavlaha ale nadoba nemala ani 50%
#define WRN_RANO_NEDOST_NADOBA -2  // varovanie: mala sa spustit rano zavlaha ale nadoba nemala ani 50%
#define WRN_ZAVL_PRAZDNA_NADOBA -3  // varovanie: pri zavlazovani sa vyprazdnila nadoba 

LiquidCrystal lcd(5, 4, 3, 2, 1, 0);
int gDistanceCm;
int gPercentoNaplnenia;
int sekundOdVypnutiaStudne;
boolean fHladinaBolaMax;
boolean fCerpadlo;
boolean fStudnaNaplna;
boolean senzorStudna;

int Status; // kod chyby alebo varovania

int gZavlaha = 0;  // aktualna sekcia zavlahy ktora polieva, ak 0 - nepolieva ziadna
int gZavlahaMod = 2;  // 1 polievaj urceny cas v stanovenu dobu aj rano aj vecer,
// 2 vypolievat celu nadrz aj rano aj vecer,
// 3 ak je malo vody v nadrzi zozpolievaj obsah nadoby na 3 sekcie rano a 3 sekcie vecer
int zavlahaVecer[2] = {22, 00}; //cas vecernej zavlahy
int zavlahaRano[2] = {06, 00}; // cas rannej zavlahy
int gCasZavlahy[6]; // casy pri ktorych sa prepne dalsia sekcia pri mode 1
int gPerZavlahy[6]; // percenta pri ktorych sa prepne dalsia sekcia pri modoch 2,3
int gValueMin, gValueMax;
int gMode = 0;  // mod zavlahy 0 - nepolievaj, 1 - iba vecer, 2 - vecer aj rano

// ***** funkcie ******
void zapniSekciu(int i);
int distanceCm();
bool jeVodaNaMax();
bool jeVodaNaMin();
int percentoNaplnenia();
void naplnajNadobu();
void prepisDisplay(void);

// ***** Setup ******
void setup() {
  // put your setup code here, to run once:

  lcd.begin(16, 2);
  lcd.print("Initializing...");
  lcd.setCursor(0, 1);
  lcd.print("Ver.");
  lcd.print(VERSION);

  setTime(00, 00, 00, 1, 1, 2016);
  delay(1000);
  pinMode(TRIGPIN, OUTPUT);
  pinMode(ECHOPIN, INPUT);
  pinMode(8, OUTPUT); // rele 1-6 BCD A
  pinMode(9, OUTPUT); // rele 1-6 BCD B
  pinMode(10, OUTPUT); // rele 1-6 BCD C
  pinMode(11, OUTPUT); // rele 7 cerpadlo studna
  pinMode(12, OUTPUT); // rele 8 pripajanie GND vodica senzorov vlhkosti v studni
  pinMode(13, INPUT); // nastavovacie tlacitko

  for (int i = 8; i <= 10; i++)
    digitalWrite(i, LOW); // vypni vsetky sekcie

  digitalWrite(11, HIGH); // vypni cerpadlo studne
  digitalWrite(12, LOW); // pripoj kontakty senzorov vlhosti v studni
  senzorStudna = true;
  fCerpadlo = false;
  sekundOdVypnutiaStudne = 0;

  nastavCasPriSpustani();  // nastavenie casu pri spustani

  Status = 0;
}

// ***** Main loop ******
void loop() {
  // put your main code here, to run repeatedly:
  int oldSecond = 0;
  int i, percSekcia;


  while (1) {

    // management nadoby a cerpadla v studni
    gPercentoNaplnenia = percentoNaplnenia();
    gPercentoNaplnenia = 100;
    
    naplnajNadobu();

    if (digitalRead(13) == 0) { // ak sa stlaci tlacitko zapne alebo vypne to zavlahu v tom momente
      if (gZavlaha > 0) {
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Stop");
        lcd.setCursor(0, 1);
        lcd.print("watering...");
        delay(10000);
        gZavlaha = 0;
        zapniSekciu(0);
      }
      else if (gZavlahaMod == 2) { // Mod 2 vypolievat celu nadrz, kriterium je bud percento alebo cas
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Starting");
        lcd.setCursor(0, 1);
        lcd.print("watering...");
        delay(1000);
        gZavlaha = 1;
        percSekcia = (gPercentoNaplnenia - 3) / 6;

        // sekcia 1 vzadu pri domceku
        gPerZavlahy[0] = gPercentoNaplnenia - (percSekcia * 1.6);
        gCasZavlahy[0] = 15 * 60;

        //sekcia 2 pri terase
        gPerZavlahy[1] = gPerZavlahy[0] - (percSekcia * 1.3);
        gCasZavlahy[1] = 12 * 60;

        //sekcia 3 bok a skalka
        gPerZavlahy[2] = gPerZavlahy[1] - (percSekcia * 0.5);
        gCasZavlahy[2] = 3 * 60;

        //sekcia 4 vpredu rotatory
        gPerZavlahy[3] = gPerZavlahy[2] - (percSekcia * 0.8);
        gCasZavlahy[3] = 12 * 60;

        // sekcia 5 vzadu pri komposte
        gPerZavlahy[4] = gPerZavlahy[3] - (percSekcia * 1.1);
        gCasZavlahy[4] = 12 * 60;

        // sekcia 6 kvapkova
        gPerZavlahy[5] = gPerZavlahy[4] - (percSekcia * 0.5);
        gCasZavlahy[5] = 1 * 5;

        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print(gPercentoNaplnenia);
        lcd.print(" ");
        lcd.print(gPerZavlahy[0]);
        lcd.print(" ");
        lcd.print(gPerZavlahy[1]);
        lcd.print(" ");
        lcd.print(gPerZavlahy[2]);
        lcd.setCursor(0, 1);
        lcd.print(gPerZavlahy[3]);
        lcd.print(" ");
        lcd.print(gPerZavlahy[4]);
        lcd.print(" ");
        lcd.print(gPerZavlahy[5]);

        delay(3000);

        zapniSekciu(gZavlaha);
      }
    }

    // spustanie zavlahy v stanoveny cas
    if ((hour() == zavlahaVecer[0] && minute() == zavlahaVecer[1] && second() == 0 && gMode > 0) || (hour() == zavlahaRano[0] && minute() == zavlahaRano[1] && second() == 0 && gMode > 1)) {
      if (gZavlahaMod == 2) { // Mod 2 vypolievat celu nadrz aj rano aj vecer, kriterium je bud percento alebo cas
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Starting");
        lcd.setCursor(0, 1);
        lcd.print("watering...");
        delay(1000);
        gZavlaha = 1;
        percSekcia = (gPercentoNaplnenia - 3) / 6;

        // sekcia 1 vzadu pri domceku
        gPerZavlahy[0] = gPercentoNaplnenia - (percSekcia * 1.6);
        gCasZavlahy[0] = 16 * 60;

        //sekcia 2 pri terase
        gPerZavlahy[1] = gPerZavlahy[0] - (percSekcia * 1.3);
        gCasZavlahy[1] = 12 * 60;

        //sekcia 3 bok a skalka
        gPerZavlahy[2] = gPerZavlahy[1] - (percSekcia * 0.5);
        gCasZavlahy[2] = 4 * 60;

        //sekcia 4 vpredu rotatory
        gPerZavlahy[3] = gPerZavlahy[2] - (percSekcia * 0.8);
        gCasZavlahy[3] = 12 * 60;

        // sekcia 5 vzadu pri komposte
        gPerZavlahy[4] = gPerZavlahy[3] - (percSekcia * 1.1);
        gCasZavlahy[4] = 11 * 60;

        // sekcia 6 kvapkova
        gPerZavlahy[5] = gPerZavlahy[4] - (percSekcia * 0.5);
        gCasZavlahy[5] = 1 * 5;

        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print(gPercentoNaplnenia);
        lcd.print(" ");
        lcd.print(gPerZavlahy[0]);
        lcd.print(" ");
        lcd.print(gPerZavlahy[1]);
        lcd.print(" ");
        lcd.print(gPerZavlahy[2]);
        lcd.setCursor(0, 1);
        lcd.print(gPerZavlahy[3]);
        lcd.print(" ");
        lcd.print(gPerZavlahy[4]);
        lcd.print(" ");
        lcd.print(gPerZavlahy[5]);

        delay(3000);

        zapniSekciu(gZavlaha);
      }
    }


    if (gZavlaha && gZavlahaMod == 2) { // mod rozpolievaj obsah nadoby rovnomerne na vsetky sekcie
      if ((gPerZavlahy[gZavlaha - 1] >= gPercentoNaplnenia || gCasZavlahy[gZavlaha - 1] == 0)  && gZavlaha < 6) {
        gZavlaha++;
        zapniSekciu(gZavlaha);
      }
      if ((gPerZavlahy[gZavlaha - 1] >= gPercentoNaplnenia || gCasZavlahy[gZavlaha - 1] == 0) && gZavlaha == 6) {
        gZavlaha = 0;
        zapniSekciu(0);
      }
    }

    if (oldSecond != second()) { // raz za sekundu prepis display
      oldSecond = second();
      if (gZavlaha && gCasZavlahy[gZavlaha - 1] > 0) {
        gCasZavlahy[gZavlaha - 1]--;
      }
      prepisDisplay();  // obsluha displeja
      if (sekundOdVypnutiaStudne > 0)
        sekundOdVypnutiaStudne--;
    }
    delay(120);

    if (Status > 0) { // ak nastala chyba nekonecna slucka
      prepisDisplay();
      while (1);
    }
  }

}
// ****************************** pomocne funkcie **************************

void nastavCasPriSpustani() {
  int h = 0, m = 0, s = 0;
  int timer = 0;
  lcd.begin(16, 2);
  lcd.print("Set hour");

  while (timer < 15) {
    // cas
    lcd.setCursor(0, 1);
    if (h < 10)
      lcd.print("0");
    lcd.print(h);
    lcd.print(":");
    if (m < 10)
      lcd.print("0");
    lcd.print(m);
    lcd.print(":");
    if (s < 10)
      lcd.print("0");
    lcd.print(s);

    if (digitalRead(13) == 0) {
      h++;
      if (h == 24)
        h = 0;
      timer = 0;
    }
    else
      timer++;
    delay(200);
  }

  lcd.setCursor(0, 0);
  lcd.print("Set minute");
  timer = 0;
  while (timer < 15) {
    // cas
    lcd.setCursor(0, 1);
    if (h < 10)
      lcd.print("0");
    lcd.print(h);
    lcd.print(":");
    if (m < 10)
      lcd.print("0");
    lcd.print(m);
    lcd.print(":");
    if (s < 10)
      lcd.print("0");
    lcd.print(s);

    if (digitalRead(13) == 0) {
      m++;
      if (m == 60)
        m = 0;
      timer = 0;
    }
    else
      timer++;
    delay(200);
  }

  setTime(h, m, 00, 1, 1, 2016);

  lcd.setCursor(0, 0);
  lcd.print("Set mode");
  timer = 0;
  while (timer < 15){
    // cas
    lcd.setCursor(0, 1);
    if (gMode == 0)
      lcd.print("No watering");
    if (gMode == 1)
      lcd.print("At the evening");
    if (gMode == 2)
      lcd.print("Both evening and morning");

    if (digitalRead(13) == 0) {
      gMode++;
      if (gMode > 2)
        gMode = 0;
      timer = 0;
    }
    else
      timer++;
    delay(200);
  }
}

// prevedie cislo do binarneho kodu BCD pre digitalne vystupy 8-10, 0 nic, 1 prve rele,... atd
// zapne rele odpovedajuce cislu
// 0 vsetky vypnute
void zapniSekciu(int k) {


  if (k >= 4) {
    digitalWrite(10, HIGH);
    k = k - 4;
  }
  else digitalWrite(10, LOW);

  if (k >= 2) {
    digitalWrite(9, HIGH);
    k = k - 2;
  }
  else digitalWrite(9, LOW);

  if (k >= 1) {
    digitalWrite(8, HIGH);
  }
  else digitalWrite(8, LOW);
}

// vrati true ak je voda na kontakte Max
bool jeVodaNaMax() {
  int sensorValueMax = gValueMax = analogRead(A1);

  if (sensorValueMax < VODA_PRAH)
    return 1;
  else
    return 0;
}

// vrati true ak je voda na kontakte Min
bool jeVodaNaMin() {
  int sensorValueMin = gValueMin = analogRead(A0);

  if (sensorValueMin < VODA_PRAH)
    return 1;
  else
    return 0;
}

// zisti vzdialenost v cm ultrazvukoveho senzora od hladiny
// zapise ju aj do globalnej premennej gDistanceCm
int distanceCm() {
  int duration, distance;

  digitalWrite(TRIGPIN, LOW);
  delayMicroseconds(2);

  digitalWrite(TRIGPIN, HIGH);
  delayMicroseconds(10);

  digitalWrite(TRIGPIN, LOW);
  duration = pulseIn(ECHOPIN, HIGH);

  //Calculate the distance (in cm) based on the speed of sound.
  gDistanceCm = distance = duration / 58.2;
  return distance;
}

// vrati hodnotu naplnenia nadrze v %
int percentoNaplnenia() {
  int perc;
  int rozsah = PRAZDNA - PLNA;
  float index = 100 / (float)rozsah;

  perc = ((float)distanceCm() - PLNA) * index;
  perc = 100 - perc;

  if (perc < 0)
    perc = 0;
  if (perc >= 100) { // pomocna logika spinania cerpadla , cerpalo cerpa kym nenacerpa 100% ale potom sa zapne az pri 95%
    fHladinaBolaMax = true;
    perc = 100;
  }
  if (perc <= HLADINA_AKTIVACIE)
    fHladinaBolaMax = false;


  return perc;
}

// funkcia sa stara o logiku cerpadla v studni s prihliadnutim na nadobu
void naplnajNadobu() {
  bool min, max;
  if (gZavlaha > 0) {
    digitalWrite(11, HIGH); // vypni cerpadlo studne
    fCerpadlo = false;
    digitalWrite(12, HIGH); // odpoj kontakty senzorov v studni ak nie su
    senzorStudna = false;
    sekundOdVypnutiaStudne = CAS_STUDNA;
    return;   // ak je zavlaha pustena necerpaj vodu do nadoby - vznikali vlny ktore rusili merac hladiny
  }

  if (senzorStudna){
    if (jeVodaNaMax() && !jeVodaNaMin()) {
      Status = ERROR_MAX_MIN;
      prepisDisplay();
      for (int i = 8; i <= 10; i++)
        digitalWrite(i, LOW); // vypni vsetky sekcie
      digitalWrite(11, HIGH); // vypni cerpadlo studne
      fCerpadlo = false;
      sekundOdVypnutiaStudne = CAS_STUDNA;
      while (1);
    }
  
  
    if (gPercentoNaplnenia >= 100 || !jeVodaNaMin()) { // nadoba plna alebo v studni min elektroda neni zaliata vodou
      digitalWrite(11, HIGH); // vypni cerpadlo studne
      fCerpadlo = false;
      digitalWrite(12, HIGH); // vypni kontakt senzorov vlhkosti v studni
      senzorStudna = false;
      sekundOdVypnutiaStudne = CAS_STUDNA;
      return;
    }
  }
  
  if (sekundOdVypnutiaStudne == 0 && !fCerpadlo){
    digitalWrite(12, LOW); // zapni kontakt senzorov vlhkosti v studni
    senzorStudna = true;
    delay(500);
    if (jeVodaNaMax() && jeVodaNaMin() && (gPercentoNaplnenia <= HLADINA_AKTIVACIE || (gPercentoNaplnenia > HLADINA_AKTIVACIE && gPercentoNaplnenia < 100 && fHladinaBolaMax == false))) {
      digitalWrite(11, LOW); // zapni cerpadlo studne
      fCerpadlo = true;
      return;
    }
    else{
      digitalWrite(12, HIGH); // ak sa necerpa odpoj kontakty senzorov a cakaj CAS_STUDNA
      senzorStudna = false;
      sekundOdVypnutiaStudne = CAS_STUDNA;
    }
  }
  return;
}

void prepisDisplay(void) {
  int h, m, s;
  // status
  lcd.clear();
  lcd.setCursor(0, 0);
  if (Status == 0) {
    lcd.print("OK MOD");
    lcd.print(gZavlahaMod);
  }
  else if (Status < 0) {
    lcd.print("WRN ");
    lcd.print(-1 * Status);
  }
  else if (Status > 0)
  {
    lcd.print("ERR ");
    lcd.print(Status);
  }

  // cas
  lcd.setCursor(8, 0);
  h = hour();
  if (h < 10)
    lcd.print("0");
  lcd.print(h);
  lcd.print(":");
  m = minute();
  if (m < 10)
    lcd.print("0");
  lcd.print(m);
  lcd.print(":");
  s = second();
  if (s < 10)
    lcd.print("0");
  lcd.print(s);

  // detail
  lcd.setCursor(0, 1);
  if (fCerpadlo)
    lcd.print("CP ");

  if (gZavlaha > 0 && gZavlahaMod == 1) {
    lcd.print("Z");
    lcd.print(gZavlaha);
    lcd.print(" ");
    m = gCasZavlahy[gZavlaha - 1] / 60;
    s = gCasZavlahy[gZavlaha - 1] - m * 60;
    if (m < 10)
      lcd.print("0");
    lcd.print(m);
    lcd.print(":");
    if (s < 10)
      lcd.print("0");
    lcd.print(s);
  }
  if (gZavlaha > 0 && gZavlahaMod == 2) {
    lcd.print("Z");
    lcd.print(gZavlaha);
    lcd.print(" ");
    m = gCasZavlahy[gZavlaha - 1] / 60;
    s = gCasZavlahy[gZavlaha - 1] - m * 60;
    if (m < 10)
      lcd.print("0");
    lcd.print(m);
    lcd.print(":");
    if (s < 10)
      lcd.print("0");
    lcd.print(s);

    lcd.setCursor(9, 1);
    lcd.print(gPerZavlahy[gZavlaha - 1]);
    lcd.print("%<");
  }
  else {
    if (senzorStudna || (sekundOdVypnutiaStudne >= CAS_STUDNA - 3)){
      lcd.print(gValueMin);
      lcd.print("/");
      lcd.print(gValueMax);
      }
    else{
        lcd.print("- / -");
      }
    lcd.print(" ");
    lcd.print(sekundOdVypnutiaStudne);
    }
  


  // nadoba
  if (gPercentoNaplnenia < 100)
    lcd.setCursor(13, 1);
  else
    lcd.setCursor(12, 1);
  lcd.print(gPercentoNaplnenia);
  lcd.print("%");

}
