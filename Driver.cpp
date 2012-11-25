//---------------------------------------------------------------------------
/*
    MaSzyna EU07 locomotive simulator
    Copyright (C) 2001-2004  Marcin Wozniak, Maciej Czapkiewicz and others

*/


#include "system.hpp"
#include "classes.hpp"
#pragma hdrstop

#include "Driver.h"
#include <mtable.hpp>
#include "DynObj.h"
#include <math.h>
#include "Globals.h"
#include "Event.h"
#include "Ground.h"
#include "MemCell.h"

#define LOGVELOCITY 0
#define LOGSTOPS 1
#define LOGBACKSCAN 0
/*

Modu� obs�uguj�cy sterowanie pojazdami (sk�adami poci�g�w, samochodami).
Ma dzia�a� zar�wno jako AI oraz przy prowadzeniu przez cz�owieka. W tym
drugim przypadku jedynie informuje za pomoc� napis�w o tym, co by zrobi�
w tym pierwszym. Obejmuje zar�wno maszynist� jak i kierownika poci�gu
(dawanie sygna�u do odjazdu).

Przeniesiona tutaj zosta�a zawarto�� ai_driver.pas przerobiona na C++.
R�wnie� niekt�re funkcje dotycz�ce sk�ad�w z DynObj.cpp.

*/


//zrobione:
//0. pobieranie komend z dwoma parametrami
//1. przyspieszanie do zadanej predkosci, ew. hamowanie jesli przekroczona
//2. hamowanie na zadanym odcinku do zadanej predkosci (ze stabilizacja przyspieszenia)
//3. wychodzenie z sytuacji awaryjnych: bezpiecznik nadmiarowy, poslizg
//4. przygotowanie pojazdu do drogi, zmiana kierunku ruchu
//5. dwa sposoby jazdy - manewrowy i pociagowy
//6. dwa zestawy psychiki: spokojny i agresywny
//7. przejscie na zestaw spokojny jesli wystepuje duzo poslizgow lub wybic nadmiarowego.
//8. lagodne ruszanie (przedluzony czas reakcji na 2 pierwszych nastawnikach)
//9. unikanie jazdy na oporach rozruchowych
//10. logowanie fizyki //Ra: nie przeniesione do C++
//11. kasowanie czuwaka/SHP
//12. procedury wspomagajace "patrzenie" na odlegle semafory
//13. ulepszone procedury sterowania
//14. zglaszanie problemow z dlugim staniem na sygnale S1
//15. sterowanie EN57
//16. zmiana kierunku //Ra: z przesiadk� po ukrotnieniu
//17. otwieranie/zamykanie drzwi
//18. Ra: odczepianie z zahamowaniem i podczepianie
//19. dla Humandriver: tasma szybkosciomierza - zapis do pliku!

//do zrobienia:
//1. kierownik pociagu
//2. madrzejsze unikanie grzania oporow rozruchowych i silnika
//3. unikanie szarpniec, zerwania pociagu itp
//4. obsluga innych awarii
//5. raportowanie problemow, usterek nie do rozwiazania
//7. samouczacy sie algorytm hamowania

//sta�e
const double EasyReactionTime=0.3;
const double HardReactionTime=0.2;
const double EasyAcceleration=0.5;
const double HardAcceleration=0.9;
const double PrepareTime     =2.0;
bool WriteLogFlag=false;

AnsiString StopReasonTable[]=
{//przyczyny zatrzymania ruchu AI
 "", //stopNone, //nie ma powodu - powinien jecha�
 "Off", //stopSleep, //nie zosta� odpalony, to nie pojedzie
 "Semaphore", //stopSem, //semafor zamkni�ty
 "Time", //stopTime, //czekanie na godzin� odjazdu
 "End of track", //stopEnd, //brak dalszej cz�ci toru
 "Change direction", //stopDir, //trzeba stan��, by zmieni� kierunek jazdy
 "Joining", //stopJoin, //zatrzymanie przy (p)odczepianiu
 "Block", //stopBlock, //przeszkoda na drodze ruchu
 "A command", //stopComm, //otrzymano tak� komend� (niewiadomego pochodzenia)
 "Out of station", //stopOut, //komenda wyjazdu poza stacj� (raczej nie powinna zatrzymywa�!)
 "Radiostop", //stopRadio, //komunikat przekazany radiem (Radiostop)
 "External", //stopExt, //przes�any z zewn�trz
 "Error", //stopError //z powodu b��du w obliczeniu drogi hamowania
};

//---------------------------------------------------------------------------
//---------------------------------------------------------------------------
//---------------------------------------------------------------------------

void __fastcall TSpeedPos::Clear()
{
 iFlags=0; //brak flag to brak reakcji
 fVelNext=-1.0; //pr�dko�� bez ogranicze�
 fDist=0.0;
 vPos=vector3(0,0,0);
 tTrack=NULL; //brak wska�nika
};

void __fastcall TSpeedPos::CommandCheck()
{//sprawdzenie typu komendy w evencie i okre�lenie pr�dko�ci
 AnsiString command=eEvent->CommandGet();
 double value1=eEvent->ValueGet(1);
 double value2=eEvent->ValueGet(2);
 if (command=="ShuntVelocity")
 {//pr�dko�� manewrow� zapisa�, najwy�ej AI zignoruje przy analizie tabelki
  fVelNext=value1; //powinno by� value2, bo druga okre�la "za"?
  iFlags|=0x200;
 }
 else if (command=="SetVelocity")
 {//w semaforze typu "m" jest ShuntVelocity dla Ms2 i SetVelocity dla S1
  //SetVelocity * 0    -> mo�na jecha�, ale stan�� przed
  //SetVelocity 0 20   -> stan�� przed, potem mo�na jecha� 20 (SBL)
  //SetVelocity -1 100 -> mo�na jecha�, przy nast�pnym ograniczenie (SBL)
  //SetVelocity 40 -1  -> PutValues: jecha� 40 a� do mini�cia (koniec ograniczenia(
  fVelNext=value1;
  iFlags&=~0xE00; //nie manewrowa, nie przystanek, nie zatrzyma� na SBL
  if (value1==0.0) //je�li pierwsza zerowa
   if (value2!=0.0) //a druga nie
   {//S1 na SBL, mo�na przejecha� po zatrzymaniu (tu nie mamy pr�dko�ci ani odleg�o�ci)
    fVelNext=value2; //normalnie b�dzie zezwolenie na jazd�, aby si� usun�� z tabelki
    iFlags|=0x800; //flaga, �e ma zatrzyma�; na pewno nie zezwoli na manewry
   }
 }
 else if (command.SubString(1,19)=="PassengerStopPoint:") //nie ma dost�pu do rozk�adu
 {//przystanek, najwy�ej AI zignoruje przy analizie tabelki
  if ((iFlags&0x400)==0)
   fVelNext=0.0; //TrainParams->IsStop()?0.0:-1.0; //na razie tak
  iFlags|=0x400; //niestety nie da si� w tym miejscu wsp�pracowa� z rozk�adem
 }
 else if (command=="SetProximityVelocity")
 {//ignorowa�
  fVelNext=-1;
 }
 else
 {//inna komenda w evencie skanowanym powoduje zatrzymanie i wys�anie tej komendy
  iFlags&=~0xE00; //nie manewrowa, nie przystanek, nie zatrzyma� na SBL
  fVelNext=0; //jak nieznana komenda w kom�rce sygna�owej, to ma sta�
 }
};

bool __fastcall TSpeedPos::Update(vector3 *p,vector3 *dir,double &len)
{//przeliczenie odleg�o�ci od punktu (*p), w kierunku (*dir), zaczynaj�c od pojazdu
 //dla kolejnych pozycji podawane s� wsp�rz�dne poprzedniego obiektu w (*p)
 vector3 v=vPos-*p; //wektor od poprzedniego obiektu (albo pojazdu) do punktu zmiany
 fDist=v.Length(); //d�ugo�� wektora to odleg�o�� pomi�dzy czo�em a sygna�em albo pocz�tkiem toru
 //v.SafeNormalize(); //normalizacja w celu okre�lenia znaku (nie potrzebna?)
 if (len==0.0)
 {//je�eli liczymy wzgl�dem pojazdu
  double iska=dir?dir->x*v.x+dir->z*v.z:fDist; //iloczyn skalarny to rzut na chwilow� prost� ruchu
  if (iska<0.0) //iloczyn skalarny jest ujemny, gdy punkt jest z ty�u
  {//je�li co� jest z ty�u, to dok�adna odleg�o�� nie ma ju� wi�kszego znaczenia
   fDist=-fDist; //potrzebne do badania wyjechania sk�adem poza ograniczenie
   if (iFlags&32) //32 ustawione, gdy obiekt ju� zosta� mini�ty
   {//je�li mini�ty (musi by� mini�ty r�wnie� przez ko�c�wk� sk�adu)
   }
   else
   {iFlags^=32; //32-mini�ty - b�dziemy liczy� odleg�o�� wzgl�dem przeciwnego ko�ca toru (nadal mo�e by� z przodu i ogdanicza�)
    if ((iFlags&0x43)==3) //tylko je�li (istotny) tor, bo eventy s� punktowe
     if (tTrack) //mo�e by� NULL, je�li koniec toru (????)
      vPos=(iFlags&4)?tTrack->CurrentSegment()->FastGetPoint_0():tTrack->CurrentSegment()->FastGetPoint_1(); //drugi koniec istotny
   }
  }
  else if (fDist<50.0) //przy du�ym k�cie �uku iloczyn skalarny bardziej zani�y odleg�o�� ni� ci�ciwa
   fDist=iska; //ale przy ma�ych odleg�o�ciach rzut na chwilow� prost� ruchu da dok�adniejsze warto�ci
 }
 if (fDist>0.0) //nie mo�e by� 0.0, a przypadkiem mog�o by si� trafi� i by�o by �le
  if ((iFlags&32)==0) //32 ustawione, gdy obiekt ju� zosta� mini�ty
  {//je�li obiekt nie zosta� mini�ty, mo�na od niego zlicza� narastaj�co (inaczej mo�e by� problem z wektorem kierunku)
   len=fDist=len+fDist; //zliczanie dlugo�ci narastaj�co
   *p=vPos; //nowy punkt odniesienia
   *dir=Normalize(v); //nowy wektor kierunku od poprzedniego obiektu do aktualnego
  }
 if (iFlags&2) //je�li tor
 {
  if (tTrack) //mo�e by� NULL, je�li koniec toru (???)
  {if (iFlags&8) //je�li odcinek zmienny
   {if (bool(tTrack->GetSwitchState()&1)!=bool(iFlags&16)) //czy stan si� zmieni�?
    {//Ra: zak�adam, �e s� tylko 2 mo�liwe stany
     iFlags^=16;
     fVelNext=tTrack->VelocityGet(); //nowa pr�dko��
     return true; //jeszcze trzeba skanowanie wykona� od tego toru
    }
    if ((iFlags&32)?false:tTrack->iNumDynamics>0) //je�li jeszcze nie wjechano na tor, a co� na nim jest
     fDist-=30.0,fVelNext=0; //to niech stanie w zwi�kszonej odleg�o�ci
    else if (fVelNext==0.0) //je�li zosta�a wyzerowana
     fVelNext=tTrack->VelocityGet(); //odczyt pr�dko�ci
   }
  }
#if LOGVELOCITY
  WriteLog("-> Dist="+FloatToStrF(fDist,ffFixed,7,1)+", Track="+tTrack->NameGet()+", Vel="+AnsiString(fVelNext)+", Flags="+AnsiString(iFlags));
#endif
 }
 else if (iFlags&0x100) //je�li event
 {//odczyt kom�rki pami�ci najlepiej by by�o zrobi� jako notyfikacj�, czyli zmiana kom�rki wywo�a jak�� podan� funkcj�
  CommandCheck(); //sprawdzenie typu komendy w evencie i okre�lenie pr�dko�ci
#if LOGVELOCITY
  WriteLog("-> Dist="+FloatToStrF(fDist,ffFixed,7,1)+", Event="+eEvent->asName+", Vel="+AnsiString(fVelNext)+", Flags="+AnsiString(iFlags));
#endif
 }
 return false;
};

bool __fastcall TSpeedPos::Set(TEvent *e,double d)
{//zapami�tanie zdarzenia
 fDist=d;
 iFlags=0x101; //event+istotny
 eEvent=e;
 vPos=e->PositionGet(); //wsp�rz�dne eventu albo kom�rki pami�ci (zrzutowa� na tor?)
 CommandCheck(); //sprawdzenie typu komendy w evencie i okre�lenie pr�dko�ci
 return fVelNext==0; //true gdy zatrzymanie, wtedy nie ma po co skanowa� dalej
};

void __fastcall TSpeedPos::Set(TTrack *t,double d,int f)
{//zapami�tanie zmiany pr�dko�ci w torze
 fDist=d; //odleg�o�� do pocz�tku toru
 tTrack=t; //TODO: (t) mo�e by� NULL i nie odczytamy ko�ca poprzedniego :/
 if (tTrack)
 {iFlags=f|(tTrack->eType==tt_Normal?2:10); //zapami�tanie kierunku wraz z typem
  if (iFlags&8) if (tTrack->GetSwitchState()&1) iFlags|=16;
  fVelNext=tTrack->VelocityGet();
  if (tTrack->iDamageFlag&128) fVelNext=0.0; //je�li uszkodzony, to te� st�j
  if (iFlags&64)
   fVelNext=(tTrack->iCategoryFlag&1)?0.0:20.0; //je�li koniec, to poci�g st�j, a samoch�d zwolnij
  vPos=(bool(iFlags&4)!=bool(iFlags&64))?tTrack->CurrentSegment()->FastGetPoint_1():tTrack->CurrentSegment()->FastGetPoint_0();
 }
};

//---------------------------------------------------------------------------
//---------------------------------------------------------------------------
//---------------------------------------------------------------------------

void __fastcall TController::TableClear()
{//wyczyszczenie tablicy
 iFirst=iLast=0;
 iTableDirection=0; //nieznany
 for (int i=0;i<iSpeedTableSize;++i) //czyszczenie tabeli pr�dko�ci
  sSpeedTable[i].Clear();
 tLast=NULL;
 fLastVel=-1;
 eSignSkip=NULL; //nic nie pomijamy
};

TEvent* __fastcall TController::CheckTrackEvent(double fDirection,TTrack *Track)
{//sprawdzanie event�w na podanym torze do podstawowego skanowania
 TEvent* e=(fDirection>0)?Track->Event2:Track->Event1;
 if (!e) return NULL;
 if (e->bEnabled) return NULL;
/* jednak wszystkie W4 do tabelki, bo jej czyszczenie na przystanku wprowadza zamieszanie
 if (e->Type==tp_PutValues)
 {//do tabelki idzie tylko rozpoznany W4
  AnsiString command=e->CommandGet();
  if (command.SubString(1,19)=="PassengerStopPoint:")
   if (asNextStop.IsEmpty()) //poci�g ma okre�lone miejsce zatrzymania
    return NULL; //ignorowa�, je�li nie ma zatrzymania
   else
    return (command==asNextStop)?e:NULL; //nazwa zatrzymania si� zgadza
 }
*/
 return e;
}

bool __fastcall TController::TableAddNew()
{//zwi�kszenie u�ytej tabelki o jeden rekord
 iLast=(iLast+1)%iSpeedTableSize;
 //TODO: jeszcze sprawdzi�, czy si� na iFirst nie na�o�y
 //TODO: wstawi� tu wywo�anie odtykacza - teraz jest to w TableTraceRoute()
 //TODO: je�li ostatnia pozycja zaj�ta, ustawia� dodatkowe flagi - teraz jest to w TableTraceRoute()
 return true; //false gdy si� na�o�y
};

bool __fastcall TController::TableNotFound(TEvent *e)
{//sprawdzenie, czy nie zosta� ju� dodany do tabelki (np. podw�jne W4 robi problemy)
 int i,j=(iLast+1)%iSpeedTableSize; //j, aby sprawdzi� te� ostatni� pozycj�
 for (i=iFirst;i!=j;i=(i+1)%iSpeedTableSize)
  if ((sSpeedTable[i].iFlags&0x101)==0x101) //o ile u�ywana pozycja
   if (sSpeedTable[i].eEvent==e)
    return false; //ju� jest, drugi raz dodawa� nie ma po co
 return true; //nie ma, czyli mo�na doda�
};

void __fastcall TController::TableTraceRoute(double fDistance,TDynamicObject *pVehicle)
{//skanowanie trajektorii na odleg�o�� (fDistance) od (pVehicle) w kierunku przodu sk�adu i uzupe�nianie tabelki
 if (!iDirection) //kierunek pojazdu z nap�dem
 {//je�li kierunek jazdy nie jest okreslony
  iTableDirection=0; //czekamy na ustawienie kierunku
 }
 TTrack *pTrack; //zaczynamy od ostatniego analizowanego toru
 //double fDistChVel=-1; //odleg�o�� do toru ze zmian� pr�dko�ci
 double fTrackLength; //d�ugo�� aktualnego toru (kr�tsza dla pierwszego)
 double fCurrentDistance; //aktualna przeskanowana d�ugo��
 TEvent *pEvent;
 float fLastDir; //kierunek na ostatnim torze
 if (iTableDirection!=iDirection)
 {//je�li zmiana kierunku, zaczynamy od toru ze wskazanym pojazdem
  pTrack=pVehicle->RaTrackGet(); //odcinek, na kt�rym stoi
  fLastDir=pVehicle->DirectionGet()*pVehicle->RaDirectionGet(); //ustalenie kierunku skanowania na torze
  fCurrentDistance=0; //na razie nic nie przeskanowano
  fTrackLength=pVehicle->RaTranslationGet(); //pozycja na tym torze (odleg�o�� od Point1)
  if (fLastDir>0) //je�li w kierunku Point2 toru
   fTrackLength=pTrack->Length()-fTrackLength; //przeskanowana zostanie odleg�o�� do Point2
  fLastVel=pTrack->VelocityGet(); //aktualna pr�dko��
  iTableDirection=iDirection; //ustalenie w jakim kierunku jest wype�niana tabelka wzgl�dem pojazdu
  iFirst=iLast=0;
  tLast=NULL; //�aden nie sprawdzony
 }
 else
 {//kontynuacja skanowania od ostatnio sprawdzonego toru (w ostatniej pozycji zawsze jest tor)
  if (sSpeedTable[iLast].iFlags&0x10000) //zatkanie
  {//je�li zape�ni�a si� tabelka
   if ((iLast+1)%iSpeedTableSize==iFirst) //je�li nadal jest zape�niona
    return; //nic si� nie da zrobi�
   if ((iLast+2)%iSpeedTableSize==iFirst) //musi by� jeszcze miejsce wolne na ewentualny event, bo tor jeszcze nie sprawdzony
    return; //ju� lepiej, ale jeszcze nie tym razem
   sSpeedTable[iLast].iFlags&=0xBE; //kontynuowa� pr�by doskanowania
  }
  else
   if (VelNext==0) return; //znaleziono semafor lub tor z pr�dko�ci� zero i nie ma co dalej sprawdza�
  pTrack=sSpeedTable[iLast].tTrack; //ostatnio sprawdzony tor
  if (!pTrack) return; //koniec toru, to nie ma co sprawdza� (nie ma prawa tak by�)
  fLastDir=sSpeedTable[iLast].iFlags&4?-1.0:1.0; //flaga ustawiona, gdy Point2 toru jest bli�ej
  fCurrentDistance=sSpeedTable[iLast].fDist; //aktualna odleg�o�� do jego Point1
  fTrackLength=sSpeedTable[iLast].iFlags&0x60?0.0:pTrack->Length(); //nie dolicza� d�ugo�ci gdy: 32-mini�ty pocz�tek, 64-jazda do ko�ca toru
 }
 if (fCurrentDistance<fDistance)
 {//je�li w og�le jest po co analizowa�
  --iLast; //jak co� si� znajdzie, zostanie wpisane w t� pozycj�, kt�r� w�a�nie odczytano
  while (fCurrentDistance<fDistance)
  {
   if (pTrack!=tLast) //ostatni zapisany w tabelce nie by� jeszcze sprawdzony
   {//je�li tor nie by� jeszcze sprawdzany
    if ((pEvent=CheckTrackEvent(fLastDir,pTrack))!=NULL) //je�li jest semafor na tym torze
    {//trzeba sprawdzi� tabelk�, bo dodawanie drugi raz tego samego przystanku nie jest korzystne
     if (TableNotFound(pEvent)) //je�li nie ma
      if (TableAddNew())
      {if (sSpeedTable[iLast].Set(pEvent,fCurrentDistance)) //dodanie odczytu sygna�u
        fDistance=fCurrentDistance; //je�li sygna� stop, to nie ma potrzeby dalej skanowa�
      }
    } //event dodajemy najpierw, �eby m�c sprawdzi�, czy tor zosta� dodany po odczytaniu pr�dko�ci nast�pnego
    if ((pTrack->VelocityGet()==0.0) //zatrzymanie
     || (pTrack->iDamageFlag&128) //pojazd si� uszkodzi
     || (pTrack->eType!=tt_Normal) //jaki� ruchomy
     || (pTrack->VelocityGet()!=fLastVel)) //nast�puje zmiana pr�dko�ci
    {//odcinek dodajemy do tabelki, gdy jest istotny dla ruchu
     if (TableAddNew())
      sSpeedTable[iLast].Set(pTrack,fCurrentDistance,fLastDir<0?5:1); //dodanie odcinka do tabelki
    }
    else if ((pTrack->fRadius!=0.0) //odleg�o�� na �uku lepiej aproksymowa� ci�ciwami
     || (tLast?tLast->fRadius!=0.0:false)) //koniec �uku te� jest istotny
    {//albo dla liczenia odleg�o�ci przy pomocy ci�ciw - te usuwa� po przejechaniu
     if (TableAddNew())
      sSpeedTable[iLast].Set(pTrack,fCurrentDistance,fLastDir<0?0x85:0x81); //dodanie odcinka do tabelki
    }
   }
   fCurrentDistance+=fTrackLength; //doliczenie kolejnego odcinka do przeskanowanej d�ugo�ci
   tLast=pTrack; //odhaczenie, �e sprawdzony
   //Track->ScannedFlag=true; //do pokazywania przeskanowanych tor�w
   fLastVel=pTrack->VelocityGet(); //pr�dko�� na poprzednio sprawdzonym odcinku
   if (fLastDir>0)
   {//je�li szukanie od Point1 w kierunku Point2
    pTrack=pTrack->CurrentNext(); //mo�e by� NULL
    if (pTrack) //je�li dalej brakuje toru, to zostajemy na tym samym, z t� sam� orientacj�
     if (tLast->iNextDirection)
      fLastDir=-fLastDir; //mo�na by zami�ta� i zmieni� tylko je�li jest pTrack
   }
   else //if (fDirection<0)
   {//je�li szukanie od Point2 w kierunku Point1
    pTrack=pTrack->CurrentPrev(); //mo�e by� NULL
    if (pTrack) //je�li dalej brakuje toru, to zostajemy na tym samym, z t� sam� orientacj�
     if (!tLast->iPrevDirection)
      fLastDir=-fLastDir;
   }
   if (pTrack)
   {//je�li kolejny istnieje
    if (tLast)
     if (pTrack->VelocityGet()<0?tLast->VelocityGet()>0:pTrack->VelocityGet()>tLast->VelocityGet())
     {//je�li kolejny ma wi�ksz� pr�dko�� ni� poprzedni, to zapami�ta� poprzedni (do czasu wyjechania)
      if ((sSpeedTable[iLast].iFlags&3)==3?(sSpeedTable[iLast].tTrack!=tLast):true) //je�li nie by� dodany do tabelki
       if (TableAddNew())
        sSpeedTable[iLast].Set(tLast,fCurrentDistance,(fLastDir>0?pTrack->iPrevDirection:pTrack->iNextDirection)?1:5); //zapisanie toru z ograniczeniem pr�dko�ci
     }
    if (((iLast+3)%iSpeedTableSize==iFirst)?true:((iLast+2)%iSpeedTableSize==iFirst)) //czy tabelka si� nie zatka?
    {//jest ryzyko nieznalezienia ograniczenia - ograniczy� pr�dko�� do pozwalaj�cej na zatrzymanie na ko�cu przeskanowanej drogi
     TablePurger(); //usun�� pilnie zb�dne pozycje
     if (((iLast+3)%iSpeedTableSize==iFirst)?true:((iLast+2)%iSpeedTableSize==iFirst)) //czy tabelka si� nie zatka?
     {//je�li odtykacz nie pom�g� (TODO: zwi�kszy� rozmiar tabelki)
      if (TableAddNew())
       sSpeedTable[iLast].Set(pTrack,fCurrentDistance,fLastDir<0?0x10045:0x10041); //zapisanie toru jako ko�cowego (ogranicza pr�dkos�)
      //zapisa� w logu, �e nale�y poprawi� sceneri�?
      return; //nie skanujemy dalej, bo nie ma miejsca
     }
    }
    fTrackLength=pTrack->Length(); //zwi�kszenie skanowanej odleg�o�ci tylko je�li istnieje dalszy tor
   }
   else
   {//definitywny koniec skanowania, chyba �e dalej puszczamy samoch�d po gruncie...
    if (TableAddNew()) //kolejny, bo si� cofn�li�my o 1
     sSpeedTable[iLast].Set(tLast,fCurrentDistance,fLastDir<0?0x45:0x41); //zapisanie ostatniego sprawdzonego toru
    return; //to ostatnia pozycja, bo NULL nic nie da, a mo�e si� podpi�� obrotnica, czy jakie� transportery
   }
  }
  if (TableAddNew())
   sSpeedTable[iLast].Set(pTrack,fCurrentDistance,fLastDir<0?4:0); //zapisanie ostatniego sprawdzonego toru
 }
};

void __fastcall TController::TableCheck(double fDistance)
{//przeliczenie odleg�o�ci w tabelce, ewentualnie doskanowanie (bez analizy pr�dko�ci itp.)
 if (iTableDirection!=iDirection)
  TableTraceRoute(fDistance,pVehicles[1]); //jak zmiana kierunku, to skanujemy od ko�ca sk�adu
 else
 {//trzeba sprawdzi�, czy co� si� zmieni�o
  vector3 dir=pVehicles[0]->VectorFront()*pVehicles[0]->DirectionGet(); //wektor kierunku jazdy
  vector3 pos=pVehicles[0]->HeadPosition(); //zaczynamy od pozycji pojazdu
  //double lastspeed=-1; //pr�dko�� na torze do usuni�cia
  double len=0.0; //odleg�o�� b�dziemy zlicza� narastaj�co
  for (int i=iFirst;i!=iLast;i=(i+1)%iSpeedTableSize)
  {//aktualizacja rekord�w z wyj�tkiem ostatniego
   if (sSpeedTable[i].iFlags&1) //je�li pozycja istotna
   {if (sSpeedTable[i].Update(&pos,&dir,len))
    {iLast=i; //wykryta zmiana zwrotnicy - konieczne ponowne przeskanowanie dalszej cz�ci
     break; //nie kontynuujemy p�tli, trzeba doskanowa� ci�g dalszy
    }
    if (sSpeedTable[i].iFlags&2) //je�li odcinek
    {if (sSpeedTable[i].fDist<-fLength) //a sk�ad wyjecha� ca�� d�ugo�ci� poza
     {//degradacja pozycji
      sSpeedTable[i].iFlags&=~1; //nie liczy si�
     }
     else if ((sSpeedTable[i].iFlags&0x28)==0x20) //jest z ty�u (najechany) i nie jest zwrotnic�
      if (sSpeedTable[i].fVelNext<0) //a nie ma ograniczenia pr�dko�ci
       sSpeedTable[i].iFlags=0; //to nie ma go po co trzyma� (odtykacz usunie ze �rodka)
    }
    else if (sSpeedTable[i].iFlags&0x100) //je�li event
    {if (sSpeedTable[i].fDist<(sSpeedTable[i].eEvent->Type==tp_PutValues?-fLength:0)) //je�li jest z ty�u
      if ((Controlling->CategoryFlag&1)?sSpeedTable[i].fVelNext!=0.0:sSpeedTable[i].fDist<-fLength)
      {//poci�g staje zawsze, a samoch�d tylko je�li nie przejedzie ca�� d�ugo�ci� (mo�e by� zaskoczony zmian�)
       sSpeedTable[i].iFlags&=~1; //degradacja pozycji
      }
    }
    //if (sSpeedTable[i].fDist<-20.0*fLength) //je�li to co� jest 20 razy dalej ni� d�ugo�� sk�adu
    //{sSpeedTable[i].iFlags&=~1; //to jest to jakby b��d w scenerii
    // //WriteLog("Error: too distant object in scan table");
    //}
    //if (sSpeedTable[i].fDist>20.0*fLength) //je�li to co� jest 20 razy dalej ni� d�ugo�� sk�adu
    //{sSpeedTable[i].iFlags&=~1; //to jest to jakby b��d w scenerii
    // //WriteLog("Error: too distant object in scan table");
    //}
   }
#if LOGVELOCITY
   else WriteLog("-> Empty");
#endif
   if (i==iFirst) //je�li jest pierwsz� pozycj� tabeli
   {//pozbycie si� pocz�tkowej pozycji
    if ((sSpeedTable[i].iFlags&1)==0) //je�li pozycja istotna (po Update() mo�e si� zmieni�)
     //if (iFirst!=iLast) //ostatnia musi zosta� - to za�atwia for()
     iFirst=(iFirst+1)%iSpeedTableSize; //kolejne sprawdzanie b�dzie ju� od nast�pnej pozycji
   }
  }
  sSpeedTable[iLast].Update(&pos,&dir,len); //aktualizacja ostatniego
  if (sSpeedTable[iLast].fDist<fDistance)
   TableTraceRoute(fDistance,pVehicles[1]); //doskanowanie dalszego odcinka
 }
};

TCommandType __fastcall TController::TableUpdate(double &fVelDes,double &fDist,double &fNext,double &fAcc)
{//ustalenie parametr�w, zwraca typ komendy, je�li sygna� podaje pr�dko�� do jazdy
 //fVelDes - pr�dko�� zadana
 //fDist - dystans w jakim nale�y rozwa�y� ruch
 //fNext - pr�dko�� na ko�cu tego dystansu
 //fAcc - zalecane przyspieszenie w chwili obecnej - kryterium wyboru dystansu
 double a; //przyspieszenie
 double v; //pr�dko��
 double d; //droga
 TCommandType go=cm_Unknown;
 eSignNext=NULL;
 int i,k=iLast-iFirst+1;
 if (k<0) k+=iSpeedTableSize; //ilo�� pozycji do przeanalizowania
 for (i=iFirst;k>0;--k,i=(i+1)%iSpeedTableSize)
 {//sprawdzenie rekord�w od (iFirst) do (iLast), o ile s� istotne
  if (sSpeedTable[i].iFlags&1) //badanie istotno�ci
  {//o ile dana pozycja tabelki jest istotna
   if (sSpeedTable[i].iFlags&0x400)
   {//je�li przystanek, trzeba obs�u�y� wg rozk�adu
    if (sSpeedTable[i].eEvent->CommandGet()!=asNextStop) //tylko gdy nazwa zatrzymania si� zgadza
    {
     sSpeedTable[i].fVelNext=-1; //aby zosta�y usuni�te z tabelki
     continue; //ignorowanie jakby nie by�o tej pozycji
    }
    else if (iDrivigFlags&moveStopPoint) //je�li pomijanie W4, to nie sprawdza czasu odjazdu
    {if (!TrainParams->IsStop())
     {//je�li nie ma tu postoju
      sSpeedTable[i].fVelNext=-1; //maksymalna pr�dko�� w tym miejscu
      if (sSpeedTable[i].fDist<200.0) //przy 160km/h jedzie 44m/s, to da dok�adno�� rz�du 5 sekund
      {//zaliczamy posterunek w pewnej odleg�o�ci przed (cho� W4 nie zas�ania ju� semafora)
#if LOGSTOPS
       WriteLog(pVehicle->asName+" as "+TrainParams->TrainName+": at "+AnsiString(GlobalTime->hh)+":"+AnsiString(GlobalTime->mm)+" skipped "+asNextStop); //informacja
#endif
       TrainParams->UpdateMTable(GlobalTime->hh,GlobalTime->mm,asNextStop.SubString(20,asNextStop.Length()));
       TrainParams->StationIndexInc(); //przej�cie do nast�pnej
       asNextStop=TrainParams->NextStop(); //pobranie kolejnego miejsca zatrzymania
       //TableClear(); //aby od nowa sprawdzi�o W4 z inn� nazw� ju� - to nie jest dobry pomys�
       sSpeedTable[i].iFlags=0; //nie liczy si� ju�
       sSpeedTable[i].fVelNext=-1; //jecha�
       continue; //nie analizowa� pr�dko�ci
      }
     } //koniec obs�ugi przelotu na W4
     else
     {//zatrzymanie na W4
      if (!eSignNext) eSignNext=sSpeedTable[i].eEvent;
      if (Controlling->Vel>0.0) //je�li jedzie
       sSpeedTable[i].fVelNext=0; //to b�dzie zatrzymanie
      else if ((iDrivigFlags&moveStopCloser)?sSpeedTable[i].fDist<=fMaxProximityDist:true)
      // sSpeedTable[i].fVelNext=0; //to b�dzie zatrzymanie
      //else //if (Controlling->Vel==0.0)
      {//je�li si� zatrzyma� przy W4, albo sta� w momencie zobaczenia W4
       if (AIControllFlag) //AI tylko sobie otwiera drzwi
        if (Controlling->TrainType==dt_EZT) //otwieranie drzwi w EN57
         if (!Controlling->DoorLeftOpened&&!Controlling->DoorRightOpened)
         {//otwieranie drzwi
          int p2=floor(sSpeedTable[i].eEvent->ValueGet(2)); //p7=platform side (1:left, 2:right, 3:both)
          if (iDirection>=0)
          {if (p2&1) Controlling->DoorLeft(true);
           if (p2&2) Controlling->DoorRight(true);
          }
          else
          {//je�li jedzie do ty�u, to drzwi otwiera odwrotnie
           if (p2&1) Controlling->DoorRight(true);
           if (p2&2) Controlling->DoorLeft(true);
          }
          if (p2&3) //�eby jeszcze poczeka� chwil�, zanim zamknie
           WaitingSet(10); //10 sekund (wzi�� z rozk�adu????)
         }
       if (TrainParams->UpdateMTable(GlobalTime->hh,GlobalTime->mm,asNextStop.SubString(20,asNextStop.Length())))
       {//to si� wykona tylko raz po zatrzymaniu na W4
        if (TrainParams->DirectionChange()) //je�li "@" w rozk�adzie, to wykonanie dalszych komend
        {//wykonanie kolejnej komendy, nie dotyczy ostatniej stacji
         JumpToNextOrder(); //przej�cie do kolejnego rozkazu (zmiana kierunku, odczepianie)
         iDrivigFlags&=~moveStopCloser; //ma nie podje�d�a� pod W4 po przeciwnej stronie
         if (Controlling->TrainType!=dt_EZT) //EZT ma sta� przy peronie, a dla lokomotyw...
          iDrivigFlags&=~(moveStopPoint|moveStopHere); //pozwolenie na przejechanie za W4 przed czasem i nie ma sta�
         sSpeedTable[i].iFlags=0; //ten W4 nie liczy si� ju� zupe�nie (nie wy�le SetVelocity)
         sSpeedTable[i].fVelNext=-1; //jecha�
         continue; //nie analizowa� pr�dko�ci
        }
       }
       if (OrderCurrentGet()==Shunt)
       {OrderNext(Obey_train); //uruchomi� jazd� poci�gow�
        CheckVehicles(); //zmieni� �wiat�a
       }
       if (TrainParams->StationIndex<TrainParams->StationCount)
       {//je�li s� dalsze stacje, czekamy do godziny odjazdu
        if (TrainParams->IsTimeToGo(GlobalTime->hh,GlobalTime->mm))
        {//z dalsz� akcj� czekamy do godziny odjazdu
         TrainParams->StationIndexInc(); //przej�cie do nast�pnej
         asNextStop=TrainParams->NextStop(); //pobranie kolejnego miejsca zatrzymania
         //TableClear(); //aby od nowa sprawdzi�o W4 z inn� nazw� ju� - to nie jest dobry pomys�
#if LOGSTOPS
         WriteLog(pVehicle->asName+" as "+TrainParams->TrainName+": at "+AnsiString(GlobalTime->hh)+":"+AnsiString(GlobalTime->mm)+" next "+asNextStop); //informacja
#endif
         iDrivigFlags|=moveStopHere; //nie podje�d�a� do semafora, je�li droga nie jest wolna
         iDrivigFlags|=moveStopCloser; //do nast�pnego W4 podjecha� blisko (z doci�ganiem)
         iDrivigFlags&=~moveStartHorn; //bez tr�bienia przed odjazdem
         sSpeedTable[i].iFlags=0; //nie liczy si� ju� zupe�nie (nie wy�le SetVelocity)
         sSpeedTable[i].fVelNext=-1; //mo�na jecha� za W4
         continue; //nie analizowa� pr�dko�ci
        } //koniec startu z zatrzymania
       } //koniec obs�ugi pocz�tkowych stacji
       else
       {//je�li dojechali�my do ko�ca rozk�adu
#if LOGSTOPS
        WriteLog(pVehicle->asName+" as "+TrainParams->TrainName+": at "+AnsiString(GlobalTime->hh)+":"+AnsiString(GlobalTime->mm)+" end of route."); //informacja
#endif
        asNextStop=TrainParams->NextStop(); //informacja o ko�cu trasy
        TrainParams->NewName("none"); //czyszczenie nieaktualnego rozk�adu
        //TableClear(); //aby od nowa sprawdzi�o W4 z inn� nazw� ju� - to nie jest dobry pomys�
        iDrivigFlags|=moveStopHere|moveStartHorn; //ma si� nie rusza� a� do momentu podania sygna�u
        iDrivigFlags&=~(moveStopCloser|moveStopPoint); //ma nie podje�d�a� pod W4 i ma je pomija�
        sSpeedTable[i].iFlags=0; //W4 nie liczy si� ju� (nie wy�le SetVelocity)
        sSpeedTable[i].fVelNext=-1; //mo�na jecha� za W4
        WaitingSet(60); //tak ze 2 minuty, a� wszyscy wysi�d�
        JumpToNextOrder(); //wykonanie kolejnego rozkazu (Change_direction albo Shunt)
        continue; //nie analizowa� pr�dko�ci
       } //koniec obs�ugi ostatniej stacji
      } //if (MoverParameters->Vel==0.0)
     } //koniec obs�ugi zatrzymania na W4
    } //koniec warunku pomijania W4 podczas zmiany czo�a
    else
    {//skoro pomijanie, to jecha� i ignorowa� W4
     sSpeedTable[i].iFlags=0; //W4 nie liczy si� ju� (nie zatrzymuje jazdy)
     sSpeedTable[i].fVelNext=-1;
     continue; //nie analizowa� pr�dko�ci
    }
   } //koniec obs�ugi W4
   v=sSpeedTable[i].fVelNext; //odczyt pr�dko�ci do zmiennej pomocniczej
   if (sSpeedTable[i].iFlags&0x100) //W4 mo�e si� deaktywowa�
   {//je�eli event, mo�e by� potrzeba wys�ania komendy, aby ruszy�
    if (sSpeedTable[i].iFlags&0x800)
    {//je�li S1 na SBL
     if (Controlling->Vel<2.0) //stan�� nie musi, ale zwolni� prznyajmniej
      if (sSpeedTable[i].fDist<fMaxProximityDist) //jest w maksymalnym zasi�gu
       eSignSkip=sSpeedTable[i].eEvent; //to mo�na go pomin�� (wzi�� drug� pr�dkos�)
     if (eSignSkip!=sSpeedTable[i].eEvent) //je�li ten SBL nie jest do pomini�cia
      v=sSpeedTable[i].eEvent->ValueGet(1); //to ma 0 odczytywa�
    }
    if ((Controlling->CategoryFlag&1)?sSpeedTable[i].fDist>pVehicles[0]->fTrackBlock-20.0:false) //jak sygna� jest dalej ni� zawalidroga
     v=0.0; //to mo�e by� podany dla tamtego: jecha� tak, jakby tam stop by�
    else
    {//zawalidrogi nie ma (albo jest samochodem), sprawdzi� sygna�
     if (sSpeedTable[i].iFlags&0x200) //je�li Tm - w zasadzie to sprawdzi� komend�!
     {//je�li podana pr�dko�� manewrowa
      if ((OrderCurrentGet()&Obey_train)?v==0.0:false)
      {//je�li tryb poci�gowy a tarcze ma ShuntVelocity 0 0
       v=-1; //ignorowa�, chyba �e pr�dko�� stanie si� niezerowa
       if (sSpeedTable[i].iFlags&0x20) //a jak przejechana
        sSpeedTable[i].iFlags=0; //to mo�na usun��, bo podstawowy automat usuwa tylko niezwrowe
      }
      else
       if (go==cm_Unknown)
        if (v!=0.0) //komenda jest tylko gdy ma jecha�, bo stoi na podstawie tabelki
        {//je�li nie by�o komendy wcze�niej - pierwsza si� liczy - ustawianie VelActual
         go=cm_ShuntVelocity; //w trybie poci�gowym tylko je�li w��cza tryb manewrowy (v!=0.0)
         VelActual=v; //nie do ko�ca tak, to jest druga pr�dko��
        }
     }
     else //if (sSpeedTable[i].iFlags&0x100) //je�li semafor !!! Komend� trzeba sprawdzi� !!!!
      if (go==cm_Unknown) //je�li nie by�o komendy wcze�niej - pierwsza si� liczy - ustawianie VelActual
       if (v<0.0?true:v>=1.0) //bo warto�� 0.1 s�u�y do hamowania tylko
       {go=cm_SetVelocity; //mo�e odjecha�
        VelActual=v; //nie do ko�ca tak, to jest druga pr�dko��; -1 nie wpisywa�...
       }
       else if (sSpeedTable[i].eEvent->StopCommand())
       {//je�li pr�dko�� jest zerowa, a kom�rka zawiera komend�
        eSignNext=sSpeedTable[i].eEvent; //dla informacji
        go=cm_Command; //komenda z kom�rki, do wykonania po zatrzymaniu
       }
    } //je�li nie ma zawalidrogi
   } //je�li event
   if (v>=0.0)
   {//pozycje z pr�dko�ci� -1 mo�na spokojnie pomija�
    d=sSpeedTable[i].fDist;
    if ((sSpeedTable[i].iFlags&0x20)?false:d>0.0) //sygna� lub ograniczenie z przodu (+32=przejechane)
     a=(v*v-Controlling->Vel*Controlling->Vel)/(25.92*d); //przyspieszenie: ujemne, gdy trzeba hamowa�
    else
     if (sSpeedTable[i].iFlags&2) //je�li tor
     {//tor ogranicza pr�dko��, dop�ki ca�y sk�ad nie przejedzie,
      //d=fLength+d; //zamiana na d�ugo�� liczon� do przodu
      if (v>1.0) //EU06 si� zawiesza�o po dojechaniu na koniec toru postojowego
       if (d<-fLength)
        continue; //zap�tlenie, je�li ju� wyjecha� za ten odcinek
      if (v<fVelDes) fVelDes=v; //ograniczenie aktualnej pr�dko�ci a� do wyjechania za ograniczenie
      //if (v==0.0) fAcc=-0.9; //hamowanie je�li stop
      continue; //i tyle wystarczy
     }
     else //event trzyma tylko je�li VelNext=0, nawet po przejechaniu (nie powinno dotyczy� samochod�w?)
      a=(v==0.0?-1.0:fAcc); //ruszanie albo hamowanie
    if (a<fAcc)
    {//mniejsze przyspieszenie to mniejsza mo�liwo�� rozp�dzenia si� albo konieczno�� hamowania
     //je�li droga wolna, to mo�e by� a>1.0 i si� tu nie za�apuje
     //if (Controlling->Vel>10.0)
     fAcc=a; //zalecane przyspieszenie (nie musi by� uwzgl�dniane przez AI)
     fNext=v; //istotna jest pr�dko�� na ko�cu tego odcinka
     fDist=d; //dlugo�� odcinka
    }
    else if ((fAcc>0)&&(v>0)&&(v<=fNext))
    {//je�li nie ma wskaza� do hamowania, mo�na poda� drog� i pr�dko�� na jej ko�cu
     fNext=v; //istotna jest pr�dko�� na ko�cu tego odcinka
     fDist=d; //dlugo�� odcinka (kolejne pozycje mog� wyd�u�a� drog�, je�li pr�dko�� jest sta�a)
    }
   } //if (v>=0.0)
   if (fNext>=0.0)
   {//je�li ograniczenie
    if ((sSpeedTable[i].iFlags&0x101)==0x101) //tylko sygna� przypisujemy
     if (!eSignNext) //je�li jeszcze nic nie zapisane tam
      eSignNext=sSpeedTable[i].eEvent; //dla informacji
    if (fNext==0.0)
     break; //nie ma sensu analizowa� tabelki dalej
   }
  } //if (sSpeedTable[i].iFlags&1)
 } //for
 return go;
};

void __fastcall TController::TablePurger()
{//odtykacz: usuwa mniej istotne pozycje ze �rodka tabelki, aby unikn�� zatkania
 //(np. brak ograniczenia pomi�dzy zwrotnicami, usuni�te sygna�y, mini�te odcinki �uku)
 int i,j,k=iLast-iFirst; //mo�e by� 15 albo 16 pozycji, ostatniej nie ma co sprawdza�
 if (k<0) k+=iSpeedTableSize; //ilo�� pozycji do przeanalizowania
 for (i=iFirst;k>0;--k,i=(i+1)%iSpeedTableSize)
 {//sprawdzenie rekord�w od (iFirst) do (iLast), o ile s� istotne
   if ((sSpeedTable[i].iFlags&1)?(sSpeedTable[i].fVelNext<0)&&((sSpeedTable[i].iFlags&0xAB)==0xA3):true)
   {//je�li jest to mini�ty (0x20) tor (0x03) do liczenia ci�ciw (0x80), a nie zwrotnica (0x08)
    for (;k>0;--k,i=(i+1)%iSpeedTableSize)
     sSpeedTable[i]=sSpeedTable[(i+1)%iSpeedTableSize]; //skopiowanie
#if LOGVELOCITY
    WriteLog("Odtykacz usuwa pozycj�");
#endif
    iLast=(iLast-1+iSpeedTableSize)%iSpeedTableSize; //cofni�cie z zawini�ciem
    return;
   }
 }
 //je�li powy�sze odtykane nie pomo�e, mo�na usun�� co� wi�cej, albo powi�kszy� tabelk�
 TSpeedPos *t=new TSpeedPos[iSpeedTableSize+16]; //zwi�kszenie
 k=iLast-iFirst+1; //tym razem wszystkie
 if (k<0) k+=iSpeedTableSize; //ilo�� pozycji do przeanalizowania
 for (j=-1,i=iFirst;k>0;--k)
 {//przepisywanie rekord�w iFirst..iLast na 0..k
  t[++j]=sSpeedTable[i];
  i=(i+1)%iSpeedTableSize; //kolejna pozycja mog� by� zawini�ta
 }
 iFirst=0; //teraz b�dzie od zera
 iLast=j; //ostatnia
 delete[] sSpeedTable; //to ju� nie potrzebne
 sSpeedTable=t; //bo jest nowe
 iSpeedTableSize+=16;
#if LOGVELOCITY
 WriteLog("Tabelka powi�kszona do "+AnsiString(iSpeedTableSize)+" pozycji");
#endif
};
//---------------------------------------------------------------------------
//---------------------------------------------------------------------------
//---------------------------------------------------------------------------

__fastcall TController::TController
(bool AI,
 TDynamicObject *NewControll,
 bool InitPsyche,
 bool primary //czy ma aktywnie prowadzi�?
)
{
 EngineActive=false;
 LastUpdatedTime=0.0;
 ElapsedTime=0.0;
 //inicjalizacja zmiennych
 Psyche=InitPsyche;
 VelDesired=0.0; //pr�dkos� pocz�tkowa
 VelforDriver=-1;
 LastReactionTime=0.0;
 HelpMeFlag=false;
 //fProximityDist=1; //nie u�ywane
 ActualProximityDist=1;
 vCommandLocation.x=0;
 vCommandLocation.y=0;
 vCommandLocation.z=0;
 VelActual=0.0; //normalnie na pocz�tku ma sta�, no chyba �e jedzie
 VelNext=120.0;
 AIControllFlag=AI;
 pVehicle=NewControll;
 Controlling=pVehicle->MoverParameters; //skr�t do obiektu parametr�w
 pVehicles[0]=pVehicle->GetFirstDynamic(0); //pierwszy w kierunku jazdy (Np. Pc1)
 pVehicles[1]=pVehicle->GetFirstDynamic(1); //ostatni w kierunku jazdy (ko�c�wki)
/*
 switch (Controlling->CabNo)
 {
  case -1: SendCtrlBroadcast("CabActivisation",1); break;
  case  1: SendCtrlBroadcast("CabActivisation",2); break;
  default: AIControllFlag:=False; //na wszelki wypadek
 }
*/
 iDirection=0;
 iDirectionOrder=Controlling->CabNo; //1=do przodu (w kierunku sprz�gu 0)
 VehicleName=Controlling->Name;
 //TrainParams=NewTrainParams;
 //if (TrainParams)
 // asNextStop=TrainParams->NextStop();
 //else
 TrainParams=new TTrainParameters("none"); //rozk�ad jazdy
 //OrderCommand="";
 //OrderValue=0;
 OrdersClear();
 MaxVelFlag=false; MinVelFlag=false; //Ra: to nie jest u�ywane
 iDriverFailCount=0;
 Need_TryAgain=false; //true, je�li druga pozycja w elektryku nie za�apa�a
 Need_BrakeRelease=true;
 deltalog=1.0;

 if (WriteLogFlag)
 {
  LogFile.open(AnsiString(VehicleName+".dat").c_str(),std::ios::in | std::ios::out | std::ios::trunc);
  LogFile << AnsiString(" Time [s]   Velocity [m/s]  Acceleration [m/ss]   Coupler.Dist[m]  Coupler.Force[N]  TractionForce [kN]  FrictionForce [kN]   BrakeForce [kN]    BrakePress [MPa]   PipePress [MPa]   MotorCurrent [A]    MCP SCP BCP LBP DmgFlag Command CVal1 CVal2").c_str() << "\r\n";
  LogFile.flush();
 }
/*
  if (WriteLogFlag)
  {
   assignfile(AILogFile,VehicleName+".txt");
   rewrite(AILogFile);
   writeln(AILogFile,"AI driver log: started OK");
   close(AILogFile);
  }
*/

 //ScanMe=False;
 VelMargin=2; //Controlling->Vmax*0.015;
 fWarningDuration=0.0; //nic do wytr�bienia
 WaitingExpireTime=31.0; //tyle ma czeka�, zanim si� ruszy
 WaitingTime=0.0;
 fMinProximityDist=30.0; //stawanie mi�dzy 30 a 60 m przed przeszkod�
 fMaxProximityDist=50.0;
 iVehicleCount=-2; //warto�� neutralna
 Prepare2press=false; //bez dociskania
 eStopReason=stopSleep; //na pocz�tku �pi
 fLength=0.0;
 fMass=0.0;
 eSignNext=NULL; //sygna� zmieniaj�cy pr�dko��, do pokazania na [F2]
 fShuntVelocity=40; //domy�lna pr�dko�� manewrowa
 fStopTime=0.0; //czas postoju przed dalsz� jazd� (np. na przystanku)
 iDrivigFlags=moveStopPoint; //podjed� do W4 mo�liwie blisko
 iDrivigFlags|=moveStopHere; //nie podje�d�aj do semafora, je�li droga nie jest wolna
 iDrivigFlags|=moveStartHorn; //podaj sygna� po podaniu wolnej drogi
 if (primary)
  iDrivigFlags|=movePrimary; //aktywnie prowadz�ce pojazd
 Ready=false;
 if (Controlling->CategoryFlag&2)
 {//samochody: na podst. http://www.prawko-kwartnik.info/hamowanie.html
  //fDriverBraking=0.0065; //mno�one przez (v^2+40*v) [km/h] daje prawie drog� hamowania [m]
  fDriverBraking=0.02; //co� nie hamuj� te samochody zbyt dobrze
  fDriverDist=5.0; //5m - zachowywany odst�p przed kolizj�
 }
 else
 {//poci�gi i statki
  fDriverBraking=0.05; //mno�one przez (v^2+40*v) [km/h] daje prawie drog� hamowania [m]
  fDriverDist=50.0; //50m - zachowywany odst�p przed kolizj�
 }
 SetDriverPsyche(); //na ko�cu, bo wymaga ustawienia zmiennych
 AccDesired=AccPreferred;
 fVelMax=-1; //ustalenie pr�dko�ci dla sk�adu
 fBrakeTime=0.0; //po jakim czasie przekr�ci� hamulec
 iVehicles=0; //na wszelki wypadek
 iSpeedTableSize=16;
 sSpeedTable=new TSpeedPos[iSpeedTableSize];
 TableClear();
 iRadioChannel=1; //numer aktualnego kana�u radiowego
};

void __fastcall TController::CloseLog()
{

 if (WriteLogFlag)
 {
  LogFile.close();
  //if WriteLogFlag)
  // CloseFile(AILogFile);
/*  append(AIlogFile);
  writeln(AILogFile,ElapsedTime5:2,": QUIT");
  close(AILogFile); */
 }
};

__fastcall TController::~TController()
{//wykopanie mechanika z roboty
 delete TrainParams;
 delete[] sSpeedTable;
 CloseLog();
};

AnsiString __fastcall TController::Order2Str(TOrders Order)
{//zamiana kodu rozkazu na opis
 if (Order==Wait_for_orders)     return "Wait_for_orders";
 if (Order==Prepare_engine)      return "Prepare_engine";
 if (Order==Shunt)               return "Shunt";
 if (Order==Connect)             return "Connect";
 if (Order==Disconnect)          return "Disconnect";
 if (Order==Change_direction)    return "Change_direction";
 if (Order==Obey_train)          return "Obey_train";
 if (Order==Release_engine)      return "Release_engine";
 if (Order==Jump_to_first_order) return "Jump_to_first_order";
/* Ra: wersja ze switch nie dzia�a prawid�owo (czemu?)
 switch (Order)
 {
  Wait_for_orders:     return "Wait_for_orders";
  Prepare_engine:      return "Prepare_engine";
  Shunt:               return "Shunt";
  Change_direction:    return "Change_direction";
  Obey_train:          return "Obey_train";
  Release_engine:      return "Release_engine";
  Jump_to_first_order: return "Jump_to_first_order";
 }
*/
 return "Undefined!";
}

AnsiString __fastcall TController::OrderCurrent()
{//pobranie aktualnego rozkazu celem wy�wietlenia
 return AnsiString(OrderPos)+". "+Order2Str(OrderList[OrderPos]);
};

void __fastcall TController::OrdersClear()
{//czyszczenie tabeli rozkaz�w na starcie albo po doj�ciu do ko�ca
 OrderPos=0;
 OrderTop=1; //szczyt stosu rozkaz�w
 for (int b=0;b<maxorders;b++)
  OrderList[b]=Wait_for_orders;
};

void __fastcall TController::Activation()
{//umieszczenie obsady w odpowiednim cz�onie
 iDirection=iDirectionOrder; //kierunek (wzgl�dem sprz�g�w pojazdu z AI) w�a�nie zosta� ustalony (zmieniony)
 if (iDirection)
 {//je�li jest ustalony kierunek
  TDynamicObject *d=pVehicle; //w tym siedzi AI
  int brake=Controlling->LocalBrakePos;
  if (TestFlag(d->MoverParameters->Couplers[iDirectionOrder<0?1:0].CouplingFlag,ctrain_controll))
  {Controlling->MainSwitch(false); //dezaktywacja czuwaka, je�li przej�cie do innego cz�onu
   Controlling->DecLocalBrakeLevel(10); //zwolnienie hamulca w opuszczanym poje�dzie
  }
  Controlling->CabDeactivisation(); //tak jest w Train.cpp
  //przej�cie AI na drug� stron� EN57, ET41 itp.
  while (TestFlag(d->MoverParameters->Couplers[iDirection<0?1:0].CouplingFlag,ctrain_controll))
  {//je�li pojazd z przodu jest ukrotniony, to przechodzimy do niego
   //if (Global::pUserDynamic)
   // ... //uaktywni� zmian� pojazdu, je�li to pojazd z u�ytkownikiem
   d=iDirection*d->DirectionGet()<0?d->Next():d->Prev(); //przechodzimy do nast�pnego cz�onu
   if (d?!d->Mechanik:false)
   {d->Mechanik=this; //na razie bilokacja
    d->MoverParameters->SetInternalCommand("",0,0); //usuni�cie ewentualnie zalegaj�cej komendy (Change_direction?)
    if (d->DirectionGet()!=pVehicle->DirectionGet()) //je�li s� przeciwne do siebie
     iDirection=-iDirection; //to b�dziemy jecha� w drug� stron� wzgl�dem zasiedzianego pojazdu
    pVehicle->Mechanik=NULL; //tam ju� nikogo nie ma
    pVehicle->MoverParameters->CabNo=0; //wy��czanie kabin po drodze
    //pVehicle->MoverParameters->ActiveCab=0;
    pVehicle=d; //a mechu ma nowy pojazd (no, cz�on)
   }
   else break; //jak zaj�te, albo koniec sk�adu, to mechanik dalej nie idzie (wywali� drugiego?)
  }
  Controlling=pVehicle->MoverParameters; //skr�t do obiektu parametr�w, mo�e by� nowy
  //Ra: to prze��czanie poni�ej jest tu bez sensu
  Controlling->ActiveCab=iDirection; //aktywacja kabiny w prowadzonym poje�dzie
  //Controlling->CabNo=iDirection;
  //Controlling->ActiveDir=0; //�eby sam ustawi� kierunek
  Controlling->CabActivisation(); //uruchomienie kabin w cz�onach
  DirectionForward(true); //nawrotnik do przodu, aby dobrze dociska� odczepiany sk�ad
  if (AIControllFlag) //je�li prowadzi komputer
   if (brake) //hamowanie tylko je�li by� wcze�niej zahamowany (bo mo�liwe, �e jedzie!)
    Controlling->IncLocalBrakeLevel(brake); //zahamuj jak wcze�niej
  CheckVehicles(); //sprawdzenie sk�adu, AI zapali �wiat�a
  TableClear(); //resetowanie tabelki skanowania tor�w
 }
};

void __fastcall TController::AutoRewident()
{//autorewident: nastawianie hamulc�w w sk�adzie
 int r=0,g=0,p=0; //ilo�ci wagon�w poszczeg�lnych typ�w
 TDynamicObject* d=pVehicles[0]; //pojazd na czele sk�adu
 //1. Zebranie informacji o sk�adzie poci�gu � przej�cie wzd�u� sk�adu i odczyt parametr�w:
 //   � ilo�� wagon�w -> s� zliczane, wszystkich pojazd�w jest (iVehicles)
 //   � d�ugo�� (jako suma) -> jest w (fLength)
 //   � masa (jako suma) -> jest w (fMass)
 while (d)
 {//klasyfikacja pojazd�w wg BrakeDelays i mocy (licznik)
  if (d->MoverParameters->Power<1) // - lokomotywa - Power>1 - ale mo�e by� nieczynna na ko�cu...
   if (TestFlag(d->MoverParameters->BrakeDelays,bdelay_R))
    ++r; // - wagon pospieszny - jest R
   else if (TestFlag(d->MoverParameters->BrakeDelays,bdelay_G))
    ++g; // - wagon towarowy - jest G (nie ma R)
   else
    ++p; // - wagon osobowy - reszta (bez G i bez R)
  d=d->Next(); //kolejny pojazd, pod��czony od ty�u (licz�c od czo�a)
 }
 //2. Okre�lenie typu poci�gu i nastawy:
 int ustaw; //+16 dla pasa�erskiego
 if (r+g+p==0)
  ustaw=16+bdelay_R; //lokomotywa luzem (mo�e by� wielocz�onowa)
 else
 {//je�li s� wagony
  ustaw=(g<min(4,r+p)?16:0);
  if (ustaw) //je�li towarowe < Min(4, pospieszne+osobowe)
  {//to sk�ad pasa�erski - nastawianie pasa�erskiego
   ustaw+=(g&&(r<g+p))?bdelay_P:bdelay_R;
   //je�eli towarowe>0 oraz pospiesze<=towarowe+osobowe to P (0)
   //inaczej R (2)
  }
  else
  {//inaczej towarowy - nastawianie towarowego
   if ((fLength<300.0)&&(fMass<600.0))
    ustaw|=bdelay_P; //je�eli d�ugo��<300 oraz masa<600 to P (0)
   else if ((fLength<500.0)&&(fMass<1300.0))
    ustaw|=bdelay_R; //je�eli d�ugo��<500 oraz masa<1300 to GP (2)
   else
    ustaw|=bdelay_G; //inaczej G (1)
  }
  //zasadniczo na sieci PKP kilka lat temu na P/GP je�dzi�y tylko kontenerowce o
  //rozk�adowej 90 km/h. Pozosta�e je�dzi�y 70 km/h i by�y nastawione na G.
 }
 d=pVehicles[0]; //pojazd na czele sk�adu
 p=0; //b�dziemy tu liczy� wagony od lokomotywy dla nastawy GP
 while (d)
 {//3. Nastawianie
  switch (ustaw)
  {
   case bdelay_P: //towarowy P - lokomotywa na G, reszta na P.
    d->MoverParameters->BrakeDelaySwitch(d->MoverParameters->Power>1?bdelay_G:bdelay_P);
   break;
   case bdelay_G: //towarowy G - wszystko na G, je�li nie ma to P (powinno si� wy��czy� hamulec)
    d->MoverParameters->BrakeDelaySwitch(TestFlag(d->MoverParameters->BrakeDelays,bdelay_G)?bdelay_G:bdelay_P);
   break;
   case bdelay_R: //towarowy GP - lokomotywa oraz 5 pierwszych pojazd�w przy niej na G, reszta na P
    if (d->MoverParameters->Power>1)
    {d->MoverParameters->BrakeDelaySwitch(bdelay_G);
     p=0; //a jak b�dzie druga w �rodku?
    }
    else
     d->MoverParameters->BrakeDelaySwitch(++p<=5?bdelay_G:bdelay_P);
   break;
   case 16+bdelay_R: //pasa�erski R - na R, je�li nie ma to P
    d->MoverParameters->BrakeDelaySwitch(TestFlag(d->MoverParameters->BrakeDelays,bdelay_R)?bdelay_R:bdelay_P);
   break;
   case 16+bdelay_P: //pasa�erski P - wszystko na P
    d->MoverParameters->BrakeDelaySwitch(bdelay_P);
   break;
  }
  d=d->Next(); //kolejny pojazd, pod��czony od ty�u (licz�c od czo�a)
 }
};

bool __fastcall TController::CheckVehicles()
{//sprawdzenie stanu posiadanych pojazd�w w sk�adzie i zapalenie �wiate�
 TDynamicObject* p; //roboczy wska�nik na pojazd
 iVehicles=0; //ilo�� pojazd�w w sk�adzie
 int d=Controlling->DirAbsolute; //kt�ry sprz�g jest z przodu
 if (!d) //je�li nie ma ustalonego kierunku
  d=Controlling->CabNo; //to jedziemy wg aktualnej kabiny
 iDirection=d; //ustalenie kierunku jazdy (powinno zrobi� PrepareEngine?)
 d=d>=0?0:1; //kierunek szukania czo�a (numer sprz�gu)
 pVehicles[0]=p=pVehicle->FirstFind(d); //pojazd na czele sk�adu
 //liczenie pojazd�w w sk�adzie i ustalenie parametr�w
 int dir=d=1-d; //a dalej b�dziemy zlicza� od czo�a do ty�u
 fLength=0.0; //d�ugo�� sk�adu do badania wyjechania za ograniczenie
 fMass=0.0; //ca�kowita masa do liczenia stycznej sk�adowej grawitacji
 fVelMax=-1; //ustalenie pr�dko�ci dla sk�adu
 bool main=true; //czy jest g��wnym steruj�cym
 while (p)
 {//sprawdzanie, czy jest g��wnym steruj�cym, �eby nie by�o konfliktu
  if (p->Mechanik) //je�li ma obsad�
   if (p->Mechanik!=this) //ale chodzi o inny pojazd, ni� aktualnie sprawdzaj�cy
    if (p->Mechanik->iDrivigFlags&movePrimary) //a tamten ma priorytet
     if (iDrivigFlags&movePrimary) //je�li rz�dzi
      p->Mechanik->iDrivigFlags&=~movePrimary; //dezaktywuje tamtego
     else
      main=false; //nici z rz�dzenia
  ++iVehicles; //jest jeden pojazd wi�cej
  pVehicles[1]=p; //zapami�tanie ostatniego
  fLength+=p->MoverParameters->Dim.L; //dodanie d�ugo�ci pojazdu
  fMass+=p->MoverParameters->TotalMass; //dodanie masy ��cznie z �adunkiem
  if (fVelMax<0?true:p->MoverParameters->Vmax<fVelMax)
   fVelMax=p->MoverParameters->Vmax; //ustalenie maksymalnej pr�dko�ci dla sk�adu
  p=p->Neightbour(dir); //pojazd pod��czony od wskazanej strony
 }
 if (main) iDrivigFlags|=movePrimary; //nie znaleziono innego, mo�na si� porz�dzi�
/* //tabelka z list� pojazd�w jest na razie nie potrzebna
 delete[] pVehicles;
 pVehicles=new TDynamicObject*[iVehicles];
*/
 if (iDrivigFlags&movePrimary)
 {//je�li jest aktywnie prowadz�cym pojazd, mo�e zrobi� w�asny porz�dek
  p=pVehicles[0];
  while (p)
  {
   if (TrainParams)
    if (p->asDestination=="none")
     p->asDestination=TrainParams->Relation2; //relacja docelowa, je�li nie by�o
   if (AIControllFlag) //je�li prowadzi komputer
    p->RaLightsSet(0,0); //gasimy �wiat�a
   d=p->DirectionSet(d?1:-1); //zwraca po�o�enie nast�pnego (1=zgodny,0=odwr�cony - wzgl�dem czo�a sk�adu)
   p=p->Next(); //pojazd pod��czony od ty�u (licz�c od czo�a)
  }
  if (AIControllFlag) //je�li prowadzi komputer
   if (OrderCurrentGet()==Obey_train) //je�li jazda poci�gowa
   {Lights(1+4+16,2+32+64); //�wiat�a poci�gowe (Pc1) i ko�c�wki (Pc5)
    AutoRewident(); //nastawianie hamulca do jazdy poci�gowej
   }
   else if (OrderCurrentGet()&(Shunt|Connect))
    Lights(16,(pVehicles[1]->MoverParameters->ActiveCab)?1:0); //�wiat�a manewrowe (Tb1) na poje�dzie z nap�dem
   else if (OrderCurrentGet()==Disconnect)
    Lights(16,0); //�wiat�a manewrowe (Tb1) tylko z przodu, aby nie pozostawi� sk�adu ze �wiat�em
 } //blok wykonywany, gdy aktywnie prowadzi
 return true;
}

void __fastcall TController::Lights(int head,int rear)
{//zapalenie �wiate� w sk��dzie
 pVehicles[0]->RaLightsSet(head,-1); //zapalenie przednich w pierwszym
 pVehicles[1]->RaLightsSet(-1,rear); //zapalenie ko�c�wek w ostatnim
}

int __fastcall TController::OrderDirectionChange(int newdir,TMoverParameters *Vehicle)
{//zmiana kierunku jazdy, niezale�nie od kabiny
 int testd=newdir;
 if (Vehicle->Vel<0.5)
 {//je�li prawie stoi, mo�na zmieni� kierunek, musi by� wykonane dwukrotnie, bo za pierwszym razem daje na zero
  switch (newdir*Vehicle->CabNo)
  {//DirectionBackward() i DirectionForward() to zmiany wzgl�dem kabiny
   case -1: if (!Vehicle->DirectionBackward()) testd=0; break;
   case  1: if (!Vehicle->DirectionForward()) testd=0; break;
  }
  if (testd==0)
   VelforDriver=-1; //kierunek zosta� zmieniony na ��dany, mo�na jecha�
 }
 else
  VelforDriver=0; //ma si� zatrzyma� w celu zmiany kierunku
 if ((Vehicle->ActiveDir==0)&&(VelforDriver<Vehicle->Vel)) //Ra: to jest chyba bez sensu
  IncBrake(); //niech hamuje
 if (Vehicle->ActiveDir==testd*Vehicle->CabNo)
  VelforDriver=-1; //mo�na jecha�, bo kierunek jest zgodny z ��danym
 if (Vehicle->TrainType==dt_EZT)
  if (Vehicle->ActiveDir>0)
   //if () //tylko je�li jazda poci�gowa (tego nie wiemy w momencie odpalania silnika)
    Vehicle->DirectionForward(); //Ra: z przekazaniem do silnikowego
 return (int)VelforDriver; //zwraca pr�dko�� mechanika
}

void __fastcall TController::WaitingSet(double Seconds)
{//ustawienie odczekania po zatrzymaniu (ustawienie w trakcie jazdy zatrzyma)
 fStopTime=-Seconds; //ujemna warto�� oznacza oczekiwanie (potem >=0.0)
}

void __fastcall TController::SetVelocity(double NewVel,double NewVelNext,TStopReason r)
{//ustawienie nowej pr�dko�ci
 WaitingTime=-WaitingExpireTime; //przypisujemy -WaitingExpireTime, a potem por�wnujemy z zerem
 MaxVelFlag=False; //Ra: to nie jest u�ywane
 MinVelFlag=False; //Ra: to nie jest u�ywane
/* nie u�ywane
 if ((NewVel>NewVelNext) //je�li oczekiwana wi�ksza ni� nast�pna
  || (NewVel<Controlling->Vel)) //albo aktualna jest mniejsza ni� aktualna
  fProximityDist=-800.0; //droga hamowania do zmiany pr�dko�ci
 else
  fProximityDist=-300.0; //Ra: ujemne warto�ci s� ignorowane
*/
 if (NewVel==0.0) //je�li ma stan��
 {if (r!=stopNone) //a jest pow�d podany
  eStopReason=r; //to zapami�ta� nowy pow�d
 }
 else
 {
  eStopReason=stopNone; //podana pr�dko��, to nie ma powod�w do stania
  //to ca�e poni�ej to warunki zatr�bienia przed ruszeniem
  if (OrderList[OrderPos]?OrderList[OrderPos]&(Obey_train|Shunt|Connect|Prepare_engine):true) //je�li jedzie w dowolnym trybie
   if ((Controlling->Vel<1.0)) //jesli stoi (na razie, bo chyba powinien te�, gdy hamuje przed semaforem)
    if (iDrivigFlags&moveStartHorn) //jezeli tr�bienie w��czone
     if (!(iDrivigFlags&moveStartHornDone)) //je�li nie zatr�bione
      if (Controlling->CategoryFlag&1) //tylko poci�gi tr�bi� (unimogi tylko na torach, wi�c trzeba raczej sprawdza� tor)
       if (NewVel>1.0) //o ile pr�dko�� jest znacz�ca
       {//fWarningDuration=0.3; //czas tr�bienia
        //if (AIControllFlag) //jak siedzi krasnoludek, to w��czy tr�bienie
        // Controlling->WarningSignal=pVehicle->iHornWarning; //wysoko�� tonu (2=wysoki)
        //iDrivigFlags|=moveStartHornDone; //nie tr�bi� a� do ruszenia
        iDrivigFlags|=moveStartHornNow; //zatr�b po odhamowaniu
       }
 }
 VelActual=NewVel;   //pr�dko�� zezwolona na aktualnym odcinku
 VelNext=NewVelNext; //pr�dko�� przy nast�pnym obiekcie
}

/* //funkcja do niczego nie potrzebna (ew. do przesuni�cia pojazdu o odleg�o�� NewDist)
bool __fastcall TController::SetProximityVelocity(double NewDist,double NewVelNext)
{//informacja o pr�dko�ci w pewnej odleg�o�ci
#if 0
 if (NewVelNext==0.0)
  WaitingTime=0.0; //nie trzeba ju� czeka�
 //if ((NewVelNext>=0)&&((VelNext>=0)&&(NewVelNext<VelNext))||(NewVelNext<VelActual))||(VelNext<0))
 {MaxVelFlag=False; //Ra: to nie jest u�ywane
  MinVelFlag=False; //Ra: to nie jest u�ywane
  VelNext=NewVelNext;
  fProximityDist=NewDist; //dodatnie: przeliczy� do punktu; ujemne: wzi�� dos�ownie
  return true;
 }
 //else return false
#endif
}
*/

void __fastcall TController::SetDriverPsyche()
{
 //double maxdist=0.5; //skalowanie dystansu od innego pojazdu, zmienic to!!!
 if ((Psyche==Aggressive)&&(OrderList[OrderPos]==Obey_train))
 {
  ReactionTime=HardReactionTime; //w zaleznosci od charakteru maszynisty
  AccPreferred=HardAcceleration; //agresywny
  //if (Controlling)
  if (Controlling->CategoryFlag&2)
   WaitingExpireTime=1; //tyle ma czeka� samoch�d, zanim si� ruszy
  else
   WaitingExpireTime=61; //tyle ma czeka�, zanim si� ruszy
 }
 else
 {
  ReactionTime=EasyReactionTime; //spokojny
  AccPreferred=EasyAcceleration;
  if (Controlling->CategoryFlag&2)
   WaitingExpireTime=3; //tyle ma czeka� samoch�d, zanim si� ruszy
  else
   WaitingExpireTime=65; //tyle ma czeka�, zanim si� ruszy
 }
 if (Controlling)
 {//with Controlling do
  if (Controlling->MainCtrlPos<3)
   ReactionTime=Controlling->InitialCtrlDelay+ReactionTime;
  if (Controlling->BrakeCtrlPos>1)
   ReactionTime=0.5*ReactionTime;
/*
  if (Controlling->Vel>0.1) //o ile jedziemy
  {//sprawdzenie jazdy na widoczno��
   TCoupling *c=pVehicles[0]->MoverParameters->Couplers+(pVehicles[0]->DirectionGet()>0?0:1); //sprz�g z przodu sk�adu
   if (c->Connected) //a mamy co� z przodu
    if (c->CouplingFlag==0) //je�li to co� jest pod��czone sprz�giem wirtualnym
    {//wyliczanie optymalnego przyspieszenia do jazdy na widoczno�� (Ra: na pewno tutaj?)
     double k=c->Connected->Vel; //pr�dko�� pojazdu z przodu (zak�adaj�c, �e jedzie w t� sam� stron�!!!)
     if (k<=Controlling->Vel) //por�wnanie modu��w pr�dko�ci [km/h]
     {if (pVehicles[0]->fTrackBlock<fMaxProximityDist) //por�wnianie z minimaln� odleg�o�ci� kolizyjn�
       k=-AccPreferred; //hamowanie maksymalne, bo jest za blisko
      else
      {//je�li tamten jedzie szybciej, to nie potrzeba modyfikowa� przyspieszenia
       double d=(pVehicles[0]->fTrackBlock-0.5*fabs(Controlling->V)-fMaxProximityDist); //bezpieczna odleg�o�� za poprzednim
       //a=(v2*v2-v1*v1)/(25.92*(d-0.5*v1))
       //(v2*v2-v1*v1)/2 to r�nica energii kinetycznych na jednostk� masy
       //je�li v2=50km/h,v1=60km/h,d=200m => k=(192.9-277.8)/(25.92*(200-0.5*16.7)=-0.0171 [m/s^2]
       //je�li v2=50km/h,v1=60km/h,d=100m => k=(192.9-277.8)/(25.92*(100-0.5*16.7)=-0.0357 [m/s^2]
       //je�li v2=50km/h,v1=60km/h,d=50m  => k=(192.9-277.8)/(25.92*( 50-0.5*16.7)=-0.0786 [m/s^2]
       //je�li v2=50km/h,v1=60km/h,d=25m  => k=(192.9-277.8)/(25.92*( 25-0.5*16.7)=-0.1967 [m/s^2]
       if (d>0) //bo jak ujemne, to zacznie przyspiesza�, aby si� zderzy�
        k=(k*k-Controlling->Vel*Controlling->Vel)/(25.92*d); //energia kinetyczna dzielona przez mas� i drog� daje przyspieszenie
       else
        k=0.0; //mo�e lepiej nie przyspiesza� -AccPreferred; //hamowanie
       //WriteLog(pVehicle->asName+" "+AnsiString(k));
      }
      if (d<fBrakeDist) //bo z daleka nie ma co hamowa�
       AccPreferred=Min0R(k,AccPreferred);
     }
    }
  }
*/
 }
};

bool __fastcall TController::PrepareEngine()
{//odpalanie silnika
 bool OK;
 bool voltfront,voltrear;
 voltfront=false;
 voltrear=false;
 LastReactionTime=0.0;
 ReactionTime=PrepareTime;
 //with Controlling do
 if (((Controlling->EnginePowerSource.SourceType==CurrentCollector)||(Controlling->TrainType==dt_EZT)))
 {
  if (Controlling->GetTrainsetVoltage())
  {
   voltfront=true;
   voltrear=true;
  }
 }
//   begin
//     if Couplers[0].Connected<>nil)
//     begin
//       if Couplers[0].Connected^.PantFrontVolt or Couplers[0].Connected^.PantRearVolt)
//         voltfront:=true
//       else
//         voltfront:=false;
//     end
//     else
//        voltfront:=false;
//     if Couplers[1].Connected<>nil)
//     begin
//      if Couplers[1].Connected^.PantFrontVolt or Couplers[1].Connected^.PantRearVolt)
//        voltrear:=true
//      else
//        voltrear:=false;
//     end
//     else
//        voltrear:=false;
//   end
 else
  //if EnginePowerSource.SourceType<>CurrentCollector)
  if (Controlling->TrainType!=dt_EZT)
   voltfront=true;
 if (AIControllFlag) //je�li prowadzi komputer
 {//cz�� wykonawcza dla sterowania przez komputer
  if (Controlling->EnginePowerSource.SourceType==CurrentCollector)
  {
   Controlling->PantFront(true);
   Controlling->PantRear(true);
  }
  if (Controlling->TrainType==dt_EZT)
  {
   Controlling->PantFront(true);
   Controlling->PantRear(true);
  }
 }
 //Ra: rozdziawianie drzwi po odpaleniu nie jest potrzebne
 //if (Controlling->DoorOpenCtrl==1)
 // if (!Controlling->DoorRightOpened)
 //  Controlling->DoorRight(true);  //McZapkie: taka prowizorka bo powinien wiedziec gdzie peron
//voltfront:=true;
 if (Controlling->PantFrontVolt||Controlling->PantRearVolt||voltfront||voltrear)
 {//najpierw ustalamy kierunek, je�li nie zosta� ustalony
  if (!iDirection) //je�li nie ma ustalonego kierunku
   if (Controlling->V==0)
   {//ustalenie kierunku, gdy stoi
    iDirection=Controlling->CabNo; //wg wybranej kabiny
    if (!iDirection) //je�li nie ma ustalonego kierunku
     if (Controlling->PantFrontVolt||Controlling->PantRearVolt||voltfront||voltrear)
     {if (Controlling->Couplers[1].CouplingFlag==ctrain_virtual) //je�li z ty�u nie ma nic
       iDirection=-1; //jazda w kierunku sprz�gu 1
      if (Controlling->Couplers[0].CouplingFlag==ctrain_virtual) //je�li z przodu nie ma nic
       iDirection=1; //jazda w kierunku sprz�gu 0
     }
   }
   else //ustalenie kierunku, gdy jedzie
    if (Controlling->PantFrontVolt||Controlling->PantRearVolt||voltfront||voltrear)
     if (Controlling->V<0) //jedzie do ty�u
      iDirection=-1; //jazda w kierunku sprz�gu 1
     else //jak nie do ty�u, to do przodu
      iDirection=1; //jazda w kierunku sprz�gu 0
  if (AIControllFlag) //je�li prowadzi komputer
  {//cz�� wykonawcza dla sterowania przez komputer
   if (!Controlling->Mains)
   {
    //if TrainType=dt_SN61)
    //   begin
    //      OK:=(OrderDirectionChange(ChangeDir,Controlling)=-1);
    //      OK:=IncMainCtrl(1);
    //   end;
    OK=Controlling->MainSwitch(true);
   }
   else
   {//Ra: iDirection okre�la, w kt�r� stron� jedzie sk�ad wzgl�dem sprz�g�w pojazdu z AI
    OK=(OrderDirectionChange(iDirection,Controlling)==-1);
    Controlling->CompressorSwitch(true);
    Controlling->ConverterSwitch(true);
    Controlling->CompressorSwitch(true);
   }
  }
  else OK=Controlling->Mains;
 }
 else
  OK=false;
 OK=OK&&(Controlling->ActiveDir!=0)&&(Controlling->CompressorAllow);
 if (OK)
 {
  if (eStopReason==stopSleep) //je�li dotychczas spa�
   eStopReason==stopNone; //teraz nie ma powodu do stania
  EngineActive=true;
  return true;
 }
 else
 {
  EngineActive=false;
  return false;
 }
};

bool __fastcall TController::ReleaseEngine()
{//wy��czanie silnika (test wy��czenia, a cz�� wykonawcza tylko je�li steruje komputer)
 bool OK=false;
 LastReactionTime=0.0;
 ReactionTime=PrepareTime;
 if (AIControllFlag)
 {//je�li steruje komputer
  if (Controlling->DoorOpenCtrl==1)
  {//zamykanie drzwi
   if (Controlling->DoorLeftOpened) Controlling->DoorLeft(false);
   if (Controlling->DoorRightOpened) Controlling->DoorRight(false);
  }
  if (Controlling->ActiveDir==0)
   if (Controlling->Mains)
   {
    Controlling->CompressorSwitch(false);
    Controlling->ConverterSwitch(false);
    if (Controlling->EnginePowerSource.SourceType==CurrentCollector)
    {
     Controlling->PantFront(false);
     Controlling->PantRear(false);
    }
    OK=Controlling->MainSwitch(false);
   }
   else
    OK=true;
 }
 else
  if (Controlling->ActiveDir==0)
   OK=Controlling->Mains; //tylko to testujemy dla pojazdu cz�owieka
 if (AIControllFlag)
  if (!Controlling->DecBrakeLevel())
   if (!Controlling->IncLocalBrakeLevel(1))
   {
    if (Controlling->ActiveDir==1)
     if (!Controlling->DecScndCtrl(2))
      if (!Controlling->DecMainCtrl(2))
       Controlling->DirectionBackward();
    if (Controlling->ActiveDir==-1)
     if (!Controlling->DecScndCtrl(2))
      if (!Controlling->DecMainCtrl(2))
       Controlling->DirectionForward();
   }
 OK=OK&&(Controlling->Vel<0.01);
 if (OK)
 {//je�li si� zatrzyma�
  EngineActive=false;
  eStopReason=stopSleep; //stoimy z powodu wy��czenia
  if (AIControllFlag)
   Lights(0,0); //gasimy �wiat�a
  OrderNext(Wait_for_orders); //�eby nie pr�bowa� co� robi� dalej
  TableClear(); //zapominamy ograniczenia
 }
 return OK;
}

bool __fastcall TController::IncBrake()
{//zwi�kszenie hamowania
 bool OK=false;
 switch (Controlling->BrakeSystem)
 {
  case Individual:
   OK=Controlling->IncLocalBrakeLevel(1+floor(0.5+fabs(AccDesired)));
   break;
  case Pneumatic:
   if ((Controlling->Couplers[0].Connected==NULL)&&(Controlling->Couplers[1].Connected==NULL))
    OK=Controlling->IncLocalBrakeLevel(1+floor(0.5+fabs(AccDesired))); //hamowanie lokalnym bo luzem jedzie
   else
   {if (Controlling->BrakeCtrlPos+1==Controlling->BrakeCtrlPosNo)
    {
     if (AccDesired<-1.5) //hamowanie nagle
      OK=Controlling->IncBrakeLevel();
     else
      OK=false;
    }
    else
    {
/*
    if (AccDesired>-0.2) and ((Vel<20) or (Vel-VelNext<10)))
            begin
              if BrakeCtrlPos>0)
               OK:=IncBrakeLevel
              else;
               OK:=IncLocalBrakeLevel(1);   //finezyjne hamowanie lokalnym
             end
           else
*/
     OK=Controlling->IncBrakeLevel();
    }
   }
   break;
  case ElectroPneumatic:
   if (Controlling->BrakeCtrlPos<Controlling->BrakeCtrlPosNo)
    if (Controlling->BrakePressureTable[Controlling->BrakeCtrlPos+1+2].BrakeType==ElectroPneumatic) //+2 to indeks Pascala 
     OK=Controlling->IncBrakeLevel();
    else
     OK=false;
  }
 return OK;
}

bool __fastcall TController::DecBrake()
{//zmniejszenie si�y hamowania
 bool OK=false;
 switch (Controlling->BrakeSystem)
 {
  case Individual:
   OK=Controlling->DecLocalBrakeLevel(1+floor(0.5+fabs(AccDesired)));
   break;
  case Pneumatic:
   if (Controlling->BrakeCtrlPos>0)
    OK=Controlling->DecBrakeLevel();
   if (!OK)
    OK=Controlling->DecLocalBrakeLevel(2);
   Need_BrakeRelease=true;
   break;
  case ElectroPneumatic:
   if (Controlling->BrakeCtrlPos>-1)
    if (Controlling->BrakePressureTable[Controlling->BrakeCtrlPos-1+2].BrakeType==ElectroPneumatic) //+2 to indeks Pascala
     OK=Controlling->DecBrakeLevel();
    else
    {if ((Controlling->BrakeSubsystem==ss_W))
     {//je�li Knorr
      Controlling->SwitchEPBrake((Controlling->BrakePress>0.0)?1:0);
     }
     OK=false;
    }
    if (!OK)
     OK=Controlling->DecLocalBrakeLevel(2);
   break;
 }
 return OK;
};

bool __fastcall TController::IncSpeed()
{//zwi�kszenie pr�dko�ci
 bool OK=true;
 if ((Controlling->DoorOpenCtrl==1)&&(Controlling->Vel==0.0)&&(Controlling->DoorLeftOpened||Controlling->DoorRightOpened))
 {//AI zamyka drzwi przed odjazdem
  if (Controlling->DoorClosureWarning)
   Controlling->DepartureSignal=true; //za��cenie bzyczka
  Controlling->DoorLeft(false); //zamykanie drzwi
  Controlling->DoorRight(false);
  //Ra: trzeba by ustawi� jaki� czas oczekiwania na zamkni�cie si� drzwi
  WaitingSet(2); //czekanie sekund�, mo�e troch� d�u�ej
 }
 else
 {
  Controlling->DepartureSignal=false;
  if (Controlling->SlippingWheels)
   return true; //jak po�lizg, to nie przyspieszamy
  switch (Controlling->EngineType)
  {
   case None:
    if (Controlling->MainCtrlPosNo>0) //McZapkie-041003: wagon sterowniczy
    {
     //TODO: sprawdzanie innego czlonu //if (!FuseFlagCheck())
     if (Controlling->BrakePress<0.3)
     {
      if (Controlling->ActiveDir>0) Controlling->DirectionForward(); //zeby EN57 jechaly na drugiej nastawie
      OK=Controlling->IncMainCtrl(1);
     }
    }
    break;
   case ElectricSeriesMotor:
    if (!Controlling->FuseFlag&&!Controlling->StLinFlag)
     if ((Controlling->MainCtrlPos==0)||(!Controlling->DelayCtrlFlag)) //youBy poleci� doda� 2012-09-08 v367
      //na pozycji 0 przejdzie, a na pozosta�ych b�dzie czeka�, a� si� za��cz� liniowe (zga�nie DelayCtrlFlag)
      if (Ready||Prepare2press)
       if (fabs(Controlling->Im)<(fReady<0.4?Controlling->Imin:Controlling->IminLo))
       {//Ra: wywala� nadmiarowy, bo Im mo�e by� ujemne; jak nie odhamowany, to nie przesadza� z pr�dem
        if ((Controlling->Vel<=30)||(Controlling->Imax>Controlling->ImaxLo))
        {//bocznik na szeregowej przy ciezkich bruttach albo przy wysokim rozruchu pod g�r�
         if (Controlling->MainCtrlPos?Controlling->RList[Controlling->MainCtrlPos].R>0.0:true) //oporowa
          OK=Controlling->IncMainCtrl(1); //kr�cimy nastawnik jazdy
         else //je�li bezoporowa (z wyj�tekiem 0)
          OK=false; //to da� bocznik
        }
        else
        {//przekroczone 30km/h, mo�na wej�� na jazd� r�wnoleg��
         if (Controlling->ScndCtrlPos) //je�li ustawiony bocznik
          if (Controlling->MainCtrlPos<Controlling->MainCtrlPosNo-1) //a nie jest ostatnia pozycja
           Controlling->DecScndCtrl(2); //to bocznik na zero po chamsku (kto� mia� to poprawi�...)
         OK=Controlling->IncMainCtrl(1);
        }
        if ((Controlling->MainCtrlPos>2)&&(Controlling->Im==0)) //brak pr�du na dalszych pozycjach
         Need_TryAgain=true; //nie za��czona lokomotywa albo wywali� nadmiarowy
        else if (!OK) //nie da si� wrzuci� kolejnej pozycji
         OK=Controlling->IncScndCtrl(1); //to da� bocznik
       }
   break;
   case Dumb:
   case DieselElectric:
    if (Ready||Prepare2press) //{(BrakePress<=0.01*MaxBrakePress)}
    {
     OK=Controlling->IncMainCtrl(1);
     if (!OK)
      OK=Controlling->IncScndCtrl(1);
    }
    break;
   case WheelsDriven:
    if (sin(Controlling->eAngle)>0)
     Controlling->IncMainCtrl(1+floor(0.5+fabs(AccDesired)));
    else
     Controlling->DecMainCtrl(1+floor(0.5+fabs(AccDesired)));
    break;
   case DieselEngine :
    if ((Controlling->Vel>Controlling->dizel_minVelfullengage)&&(Controlling->RList[Controlling->MainCtrlPos].Mn>0))
     OK=Controlling->IncMainCtrl(1);
    if (Controlling->RList[Controlling->MainCtrlPos].Mn==0)
     OK=Controlling->IncMainCtrl(1);
    if (!Controlling->Mains)
    {
     Controlling->MainSwitch(true);
     Controlling->ConverterSwitch(true);
     Controlling->CompressorSwitch(true);
    }
    break;
  }
 }
 return OK;
}

bool __fastcall TController::DecSpeed()
{//zmniejszenie pr�dko�ci (ale nie hamowanie)
 bool OK=false; //domy�lnie false, aby wysz�o z p�tli while
 switch (Controlling->EngineType)
 {
  case None:
   if (Controlling->MainCtrlPosNo>0) //McZapkie-041003: wagon sterowniczy, np. EZT
    OK=Controlling->DecMainCtrl(1+(Controlling->MainCtrlPos>2?1:0));
  break;
  case ElectricSeriesMotor:
   OK=Controlling->DecScndCtrl(2); //najpierw bocznik na zero
   if (!OK)
    OK=Controlling->DecMainCtrl(1+(Controlling->MainCtrlPos>2?1:0));
  break;
  case Dumb:
  case DieselElectric:
   OK=Controlling->DecScndCtrl(2);
   if (!OK)
    OK=Controlling->DecMainCtrl(2+(Controlling->MainCtrlPos/2));
  break;
  //WheelsDriven :
   // begin
   //  OK:=False;
   // end;
  case DieselEngine:
   if ((Controlling->Vel>Controlling->dizel_minVelfullengage))
   {
    if (Controlling->RList[Controlling->MainCtrlPos].Mn>0)
     OK=Controlling->DecMainCtrl(1);
   }
   else
    while ((Controlling->RList[Controlling->MainCtrlPos].Mn>0)&&(Controlling->MainCtrlPos>1))
     OK=Controlling->DecMainCtrl(1);
  break;
 }
 return OK;
};

void __fastcall TController::SpeedSet()
{//Ra: regulacja pr�dko�ci, wykonywana w ka�dym przeb�ysku �wiadomo�ci AI
 //ma dokr�ca� do bezoporowych i zdejmowa� pozycje w przypadku przekroczenia pr�du
 switch (Controlling->EngineType)
 {
  case None:
  break;
  case ElectricSeriesMotor:
   if (Controlling->StLinFlag) //styczniki liniowe nie za��czone
    while (DecSpeed()); //zerowanie nap�du
   else
    if (Ready||Prepare2press) //o ile mo�e jecha�
     if (fAccGravity<-0.10) //i jedzie pod g�r� wi�ksz� ni� 10 promil
     {//procedura wje�d�ania na ekstremalne wzniesienia
      if (fabs(Controlling->Im)>Controlling->Imin) //a pr�d jest znaczny
       if (Controlling->Imin*Controlling->Voltage/(fMass*fAccGravity)<-2.8) //a na niskim si� za szybko nie pojedzie
       {//w��czenie wysokiego rozruchu
        if (Controlling->RList[Controlling->MainCtrlPos].Bn>1)
        {do //je�li jedzie na r�wnoleg�ym, to zbijamy do szeregowego, aby w��czy� wysoki rozruch
          Controlling->DecMainCtrl(1); //kr�cimy nastawnik jazdy o 1 wstecz
         while (Controlling->MainCtrlPos?Controlling->RList[Controlling->MainCtrlPos].Bn>1:false); //oporowa zap�tla
        }
        if (Controlling->Imax<Controlling->ImaxHi) //je�li da si� na wysokim
         Controlling->CurrentSwitch(true); //rozruch wysoki (za to mo�e si� �lizga�)
        if (ReactionTime>0.1)
         ReactionTime=0.1; //orientuj si� szybciej
       } //if (Im>Imin)
      if (fabs(Controlling->Im)>0.75*Controlling->ImaxHi) //je�li pr�d jest du�y
       Controlling->SandDoseOn(); //piaskujemy tory, coby si� nie �lizga�
      if ((fabs(Controlling->Im)>0.96*Controlling->Imax)||Controlling->SlippingWheels) //je�li pr�d jest du�y (mo�na 690 na 750)
       if (Controlling->ScndCtrlPos>0) //je�eli jest bocznik
        Controlling->DecScndCtrl(2); //zmniejszy� bocznik
       else
        Controlling->DecMainCtrl(1); //kr�cimy nastawnik jazdy o 1 wstecz
     }
     else
     {//dokr�canie do bezoporowej, bo IncSpeed() mo�e nie by� wywo�ywane
      //if (Controlling->Vel<VelDesired)
      // if (AccDesired>0)
      //  if (Controlling->RList[MainCtrlPos].R>0.0)
      //   if (Im<1.3*Imin) //lekkie przekroczenie miimalnego pr�du jest dopuszczalne
      //    IncMainCtrl(1); //zwieksz nastawnik skoro mo�esz - tak aby si� ustawic na bezoporowej
     }
  break;
  case Dumb:
  case DieselElectric:
  break;
  //WheelsDriven :
   // begin
   //  OK:=False;
   // end;
  case DieselEngine:
  break;
 }
};

void __fastcall TController::RecognizeCommand()
{//odczytuje i wykonuje komend� przekazan� lokomotywie
 TCommand *c=&Controlling->CommandIn;
 PutCommand(c->Command,c->Value1,c->Value2,c->Location,stopComm);
 c->Command=""; //usuni�cie obs�u�onej komendy
}


void __fastcall TController::PutCommand(AnsiString NewCommand,double NewValue1,double NewValue2,const TLocation &NewLocation,TStopReason reason)
{//wys�anie komendy przez event PutValues, jak pojazd ma obsad�, to wysy�a tutaj, a nie do pojazdu bezpo�rednio
 vector3 sl;
 sl.x=-NewLocation.X; //zamiana na wsp�rz�dne scenerii
 sl.z= NewLocation.Y;
 sl.y= NewLocation.Z;
 if (!PutCommand(NewCommand,NewValue1,NewValue2,&sl,reason))
  Controlling->PutCommand(NewCommand,NewValue1,NewValue2,NewLocation);
}


bool __fastcall TController::PutCommand(AnsiString NewCommand,double NewValue1,double NewValue2,const vector3 *NewLocation,TStopReason reason)
{//analiza komendy
 if (NewCommand=="Emergency_brake") //wymuszenie zatrzymania, niezale�nie kto prowadzi
 {//Ra: no nadal nie jest zbyt pi�knie
  SetVelocity(0,0,reason);
  Controlling->PutCommand("Emergency_brake",1.0,1.0,Controlling->Loc);
  return true; //za�atwione
 }
 else if (NewCommand.Pos("Timetable:")==1)
 {//przypisanie nowego rozk�adu jazdy, r�wnie� prowadzonemu przez u�ytkownika
  NewCommand.Delete(1,10); //zostanie nazwa pliku z rozk�adem
#if LOGSTOPS
  WriteLog("New timetable for "+pVehicle->asName+": "+NewCommand); //informacja
#endif
  if (!TrainParams)
   TrainParams=new TTrainParameters(NewCommand); //rozk�ad jazdy
  else
   TrainParams->NewName(NewCommand); //czy�ci tabelk� przystank�w
  if (NewCommand!="none")
  {if (!TrainParams->LoadTTfile(Global::asCurrentSceneryPath,floor(NewValue2+0.5),NewValue1)) //pierwszy parametr to przesuni�cie rozk�adu w czasie
   {
    if (ConversionError==-8)
     ErrorLog("Missed file: "+NewCommand);
    WriteLog("Cannot load timetable file "+NewCommand+"\r\nError "+ConversionError+" in position "+TrainParams->StationCount);
   }
   else
   {//inicjacja pierwszego przystanku i pobranie jego nazwy
    TrainParams->UpdateMTable(GlobalTime->hh,GlobalTime->mm,TrainParams->NextStationName);
    TrainParams->StationIndexInc(); //przej�cie do nast�pnej
    asNextStop=TrainParams->NextStop();
    iDrivigFlags|=movePrimary; //skoro dosta� rozk�ad, to jest teraz g��wnym
   }
  }
  if (NewLocation) //je�li podane wsp�rz�dne eventu/kom�rki ustawiaj�cej rozk�ad (trainset nie podaje)
  {vector3 v=*NewLocation-pVehicle->GetPosition(); //wektor do punktu steruj�cego
   vector3 d=pVehicle->VectorFront(); //wektor wskazuj�cy prz�d
   iDirectionOrder=((v.x*d.x+v.z*d.z)*NewValue1>0)?1:-1; //do przodu, gdy iloczyn skalarny i pr�dko�� dodatnie
/*
  if (NewValue1!=0.0) //je�li ma jecha�
   if (iDirectionOrder==0) //a kierunek nie by� okre�lony (normalnie okre�lany przez reardriver/headdriver)
    iDirectionOrder=NewValue1>0?1:-1; //ustalenie kierunku jazdy wzgl�dem sprz�g�w
   else
    if (NewValue1<0) //dla ujemnej pr�dko�ci
     iDirectionOrder=-iDirectionOrder; //ma jecha� w drug� stron�
*/
  }
  Activation(); //umieszczenie obs�ugi we w�a�ciwym cz�onie, ustawienie nawrotnika w prz�d
  //je�li wysy�ane z Trainset, to wszystko jest ju� odpowiednio ustawione
  //if (!NewLocation) //je�li wysy�ane z Trainset
  // if (Controlling->CabNo*Controlling->V*NewValue1<0) //je�li zadana pr�dko�� niezgodna z aktualnym kierunkiem jazdy
  //  DirectionForward(false); //jedziemy do ty�u (nawrotnik do ty�u)
  //CheckVehicles(); //sprawdzenie sk�adu, AI zapali �wiat�a
  TableClear(); //wyczyszczenie tabelki pr�dko�ci, bo na nowo trzeba okre�li� kierunek i sprawdzi� przystanki
  OrdersInit(fabs(NewValue1)); //ustalenie tabelki komend wg rozk�adu oraz pr�dko�ci pocz�tkowej
  //if (NewValue1!=0.0) if (!AIControllFlag) DirectionForward(NewValue1>0.0); //ustawienie nawrotnika u�ytkownikowi (propaguje si� do cz�on�w)
  //if (NewValue1>0)
  // TrainNumber=floor(NewValue1); //i co potem ???
  return true; //za�atwione
 }
 if (NewCommand=="SetVelocity")
 {
  if (NewLocation)
   vCommandLocation=*NewLocation;
  if ((NewValue1!=0.0)&&(OrderList[OrderPos]!=Obey_train))
  {//o ile jazda
   if (!EngineActive)
    OrderNext(Prepare_engine); //trzeba odpali� silnik najpierw, �wiat�a ustawi JumpToNextOrder()
   //if (OrderList[OrderPos]!=Obey_train) //je�li nie poci�gowa
   OrderNext(Obey_train); //to uruchomi� jazd� poci�gow� (od razu albo po odpaleniu silnika
   OrderCheck(); //je�li jazda poci�gowa teraz, to wykona� niezb�dne operacje
  }
  if (NewValue1!=0.0) //je�li jecha�
   iDrivigFlags&=~moveStopHere; //podje�anie do semafor�w zezwolone
  else
   iDrivigFlags|=moveStopHere; //sta� do momentu podania komendy jazdy
  SetVelocity(NewValue1,NewValue2,reason); //bylo: nic nie rob bo SetVelocity zewnetrznie jest wywolywane przez dynobj.cpp
 }
 else if (NewCommand=="SetProximityVelocity")
 {
/*
  if (SetProximityVelocity(NewValue1,NewValue2))
   if (NewLocation)
    vCommandLocation=*NewLocation;
*/
 }
 else if (NewCommand=="ShuntVelocity")
 {//uruchomienie jazdy manewrowej b�d� zmiana pr�dko�ci
  if (NewLocation)
   vCommandLocation=*NewLocation;
  //if (OrderList[OrderPos]=Obey_train) and (NewValue1<>0))
  if (!EngineActive)
   OrderNext(Prepare_engine); //trzeba odpali� silnik najpierw
  OrderNext(Shunt); //zamieniamy w aktualnej pozycji, albo dodajey za odpaleniem silnika
  if (NewValue1!=0.0)
  {
   //if (iVehicleCount>=0) WriteLog("Skasowano ilos� wagon�w w ShuntVelocity!");
   iVehicleCount=-2; //warto�� neutralna
   CheckVehicles(); //zabra� to do OrderCheck()
  }
  //dla pr�dko�ci ujemnej przestawi� nawrotnik do ty�u? ale -1=brak ograniczenia !!!!
  //if (Prepare2press) WriteLog("Skasowano docisk w ShuntVelocity!");
  Prepare2press=false; //bez dociskania
  SetVelocity(NewValue1,NewValue2,reason);
  if (NewValue1!=0.0)
   iDrivigFlags&=~moveStopHere; //podje�anie do semafor�w zezwolone
  else
   iDrivigFlags|=moveStopHere; //ma sta� w miejscu
  if (fabs(NewValue1)>2.0) //o ile warto�� jest sensowna (-1 nie jest konkretn� warto�ci�)
   fShuntVelocity=fabs(NewValue1); //zapami�tanie obowi�zuj�cej pr�dko�ci dla manewr�w
 }
 else if (NewCommand=="Wait_for_orders")
 {//oczekiwanie; NewValue1 - czas oczekiwania, -1 = na inn� komend�
  if (NewValue1>0.0?NewValue1>fStopTime:false)
   fStopTime=NewValue1; //Ra: w��czenie czekania bez zmiany komendy
  else
   OrderList[OrderPos]=Wait_for_orders; //czekanie na komend� (albo da� OrderPos=0)
 }
 else if (NewCommand=="Prepare_engine")
 {//w��czenie albo wy��czenie silnika (w szerokim sensie)
  OrdersClear(); //czyszczenie tabelki rozkaz�w, aby nic dalej nie robi�
  if (NewValue1==0.0)
   OrderNext(Release_engine); //wy��czy� silnik (przygotowa� pojazd do jazdy)
  else if (NewValue1>0.0)
   OrderNext(Prepare_engine); //odpali� silnik (wy��czy� wszystko, co si� da)
  //po za��czeniu przejdzie do kolejnej komendy, po wy��czeniu na Wait_for_orders
 }
 else if (NewCommand=="Change_direction")
 {
  TOrders o=OrderList[OrderPos]; //co robi� przed zmian� kierunku
  if (!EngineActive)
   OrderNext(Prepare_engine); //trzeba odpali� silnik najpierw
  if (NewValue1==0.0)
   iDirectionOrder=-iDirection; //zmiana na przeciwny ni� obecny
  else
   if (NewLocation) //je�li podane wsp�rz�dne eventu/kom�rki ustawiaj�cej rozk�ad (trainset nie podaje)
   {vector3 v=*NewLocation-pVehicle->GetPosition(); //wektor do punktu steruj�cego
    vector3 d=pVehicle->VectorFront(); //wektor wskazuj�cy prz�d
    iDirectionOrder=((v.x*d.x+v.z*d.z)*NewValue1>0)?1:-1; //do przodu, gdy iloczyn skalarny i pr�dko�� dodatnie
    //iDirectionOrder=1; else if (NewValue1<0.0) iDirectionOrder=-1;
   }
  if (iDirectionOrder!=iDirection)
   OrderNext(Change_direction); //zadanie komendy do wykonania
  if (o>=Shunt) //je�li jazda manewrowa albo poci�gowa
   OrderNext(o); //to samo robi� po zmianie
  else if (!o) //je�li wcze�niej by�o czekanie
   OrderNext(Shunt); //to dalej jazda manewrowa
  iDrivigFlags&=~moveStartHorn; //bez tr�bienia po ruszeniu z zatrzymania
  //Change_direction wykona si� samo i nast�pnie przejdzie do kolejnej komendy
 }
 else if (NewCommand=="Obey_train")
 {
  if (!EngineActive)
   OrderNext(Prepare_engine); //trzeba odpali� silnik najpierw
  OrderNext(Obey_train);
  //if (NewValue1>0) TrainNumber=floor(NewValue1); //i co potem ???
  OrderCheck(); //je�li jazda poci�gowa teraz, to wykona� niezb�dne operacje
 }
 else if (NewCommand=="Shunt")
 {//NewValue1 - ilo�� wagon�w (-1=wszystkie); NewValue2: 0=odczep, 1..63=do��cz, -1=bez zmian
  //-3,-y - pod��czy� do ca�ego stoj�cego sk�adu (sprz�giem y>=1), zmieni� kierunek i czeka� w trybie poci�gowym
  //-2,-y - pod��czy� do ca�ego stoj�cego sk�adu (sprz�giem y>=1), zmieni� kierunek i czeka�
  //-2, y - pod��czy� do ca�ego stoj�cego sk�adu (sprz�giem y>=1) i czeka�
  //-1,-y - pod��czy� do ca�ego stoj�cego sk�adu (sprz�giem y>=1) i jecha� w powrotn� stron�
  //-1, y - pod��czy� do ca�ego stoj�cego sk�adu (sprz�giem y>=1) i jecha� dalej
  //-1, 0 - tryb manewrowy bez zmian (odczepianie z pozostawieniem wagon�w nie ma sensu)
  // 0, 0 - odczepienie lokomotywy
  // 1,-y - pod��czy� si� do sk�adu (sprz�giem y>=1), a nast�pnie odczepi� i zabra� (x) wagon�w
  // 1, 0 - odczepienie lokomotywy z jednym wagonem
  iDrivigFlags&=~moveStopHere; //podje�anie do semafor�w zezwolone
  if (!EngineActive)
   OrderNext(Prepare_engine); //trzeba odpali� silnik najpierw
  if (NewValue2!=0) //je�li podany jest sprz�g
  {iCoupler=floor(fabs(NewValue2)); //jakim sprz�giem
   OrderNext(Connect); //najpierw po��cz pojazdy
   if (NewValue1>=0.0) //je�li ilo�� wagon�w inna ni� wszystkie
   {//to po podpi�ciu nale�y si� odczepi�
    iDirectionOrder=-iDirection; //zmiana na ci�gni�cie
    OrderPush(Change_direction); //najpierw zmie� kierunek, bo odczepiamy z ty�u
    OrderPush(Disconnect); //a odczep ju� po zmianie kierunku
   }
   else if (NewValue2<0.0) //je�li wszystkie, a sprz�g ujemny, to tylko zmiana kierunku po podczepieniu
   {//np. Shunt -1 -3
    iDirectionOrder=-iDirection; //jak si� podczepi, to jazda w przeciwn� stron�
    OrderNext(Change_direction);
   }
   WaitingTime=0.0; //nie ma co dalej czeka�, mo�na zatr�bi� i jecha�, chyba �e ju� jedzie
  }
  else //if (NewValue2==0.0) //zerowy sprz�g
   if (NewValue1>=0.0) //je�li ilo�� wagon�w inna ni� wszystkie
   {//b�dzie odczepianie, ale je�li wagony s� z przodu, to trzeba najpierw zmieni� kierunek
    if ((Controlling->Couplers[Controlling->DirAbsolute>0?1:0].CouplingFlag==0)? //z ty�u nic
     (Controlling->Couplers[Controlling->DirAbsolute>0?0:1].CouplingFlag>0):false) //a z przodu sk�ad
    {iDirectionOrder=-iDirection; //zmiana na ci�gni�cie
     OrderNext(Change_direction); //najpierw zmie� kierunek (zast�pi Disconnect)
     OrderPush(Disconnect); //a odczep ju� po zmianie kierunku
    }
    else
     if (Controlling->Couplers[Controlling->DirAbsolute>0?1:0].CouplingFlag>0) //z ty�u co�
      OrderNext(Disconnect); //jak ci�gnie, to tylko odczep (NewValue1) wagon�w
    WaitingTime=0.0; //nie ma co dalej czeka�, mo�na zatr�bi� i jecha�, chyba �e ju� jedzie
   }
  if (NewValue1==-1.0)
  {iDrivigFlags&=~moveStopHere; //ma jecha�
   WaitingTime=0.0; //nie ma co dalej czeka�, mo�na zatr�bi� i jecha�
  }
  if (NewValue1<-1.5) //je�li -2/-3, czyli czekanie z ruszeniem na sygna�
   iDrivigFlags|=moveStopHere; //nie podje�d�a� do semafora, je�li droga nie jest wolna (nie dotyczy Connect)
  if (NewValue1<-2.5) //je�li
   OrderNext(Obey_train); //to potem jazda poci�gowa
  else
   OrderNext(Shunt); //to potem manewruj dalej
  CheckVehicles(); //sprawdzi� �wiat�a
  //if ((iVehicleCount>=0)&&(NewValue1<0)) WriteLog("Skasowano ilos� wagon�w w Shunt!");
  if (NewValue1!=iVehicleCount)
   iVehicleCount=floor(NewValue1); //i co potem ? - trzeba zaprogramowac odczepianie
/*
  if (NewValue1!=-1.0)
   if (NewValue2!=0.0)

    if (VelDesired==0)
     SetVelocity(20,0); //to niech jedzie
*/
 }
 else if (NewCommand=="Jump_to_first_order")
  JumpToFirstOrder();
 else if (NewCommand=="Jump_to_order")
 {
  if (NewValue1==-1.0)
   JumpToNextOrder();
  else
   if ((NewValue1>=0)&&(NewValue1<maxorders))
   {OrderPos=floor(NewValue1);
    if (!OrderPos) OrderPos=1; //zgodno�� wstecz: dopiero pierwsza uruchamia
   }
/*
  if (WriteLogFlag)
  {
   append(AIlogFile);
   writeln(AILogFile,ElapsedTime:5:2," - new order: ",Order2Str( OrderList[OrderPos])," @ ",OrderPos);
   close(AILogFile);
  }
*/
 }
 else if (NewCommand=="OutsideStation") //wskaznik W5
 {
  if (OrderList[OrderPos]==Obey_train)
   SetVelocity(NewValue1,NewValue2,stopOut); //koniec stacji - predkosc szlakowa
  else //manewry - zawracaj
  {
   iDirectionOrder=-iDirection; //zmiana na przeciwny ni� obecny
   OrderNext(Change_direction); //zmiana kierunku
   OrderNext(Shunt); //a dalej manewry
   iDrivigFlags&=~moveStartHorn; //bez tr�bienia po zatrzymaniu
  }
 }
 else if (NewCommand=="Warning_signal")
 {
  if (AIControllFlag) //poni�sza komenda nie jest wykonywana przez u�ytkownika
   if (NewValue1>0)
   {
    fWarningDuration=NewValue1; //czas tr�bienia
    Controlling->WarningSignal=(NewValue2>1)?2:1; //wysoko�� tonu
   }
 }
 else if (NewCommand=="Radio_channel")
 {//wyb�r kana�u radiowego (kt�rego powinien u�ywa� AI, r�czny maszynista musi go ustawi� sam)
  if (NewValue1>=0) //warto�ci ujemne s� zarezerwowane, -1 = nie zmienia� kana�u
   iRadioChannel=NewValue1;
  //NewValue2 mo�e zawiera� dodatkowo oczekiwany kod odpowiedzi, np. dla W29 "nawi�za� ��czno�� radiow� z dy�urnym ruchu odcinkowym"
 }
 else return false; //nierozpoznana - wys�a� bezpo�rednio do pojazdu
 return true; //komenda zosta�a przetworzona
};

void __fastcall TController::PhysicsLog()
{//zapis logu - na razie tylko wypisanie parametr�w
   if (LogFile.is_open())
   {
    LogFile << ElapsedTime<<" "<<fabs(11.31*Controlling->WheelDiameter*Controlling->nrot)<<" ";
    LogFile << Controlling->AccS<<" "<<Controlling->Couplers[1].Dist<<" "<<Controlling->Couplers[1].CForce<<" ";
    LogFile << Controlling->Ft<<" "<<Controlling->Ff<<" "<<Controlling->Fb<<" "<<Controlling->BrakePress<<" ";
    LogFile << Controlling->PipePress<<" "<<Controlling->Im<<" "<<int(Controlling->MainCtrlPos)<<"   ";
    LogFile << int(Controlling->ScndCtrlPos)<<"   "<<int(Controlling->BrakeCtrlPos)<<"   "<<int(Controlling->LocalBrakePos)<<"   ";
    LogFile << int(Controlling->ActiveDir)<<"   "<<Controlling->CommandIn.Command.c_str()<<" "<<Controlling->CommandIn.Value1<<" ";
    LogFile << Controlling->CommandIn.Value2<<" "<<int(Controlling->SecuritySystem.Status)<<" "<<int(Controlling->SlippingWheels)<<"\r\n";
    LogFile.flush();
   }
};

bool __fastcall TController::UpdateSituation(double dt)
{//uruchamia� przynajmniej raz na sekund�
 if ((iDrivigFlags&movePrimary)==0) return true; //pasywny nic nie robi
 double AbsAccS,VelReduced;
 bool UpdateOK=false;
 if (AIControllFlag)
 {//yb: zeby EP nie musial sie bawic z ciesnieniem w PG
//  if (Controlling->BrakeSystem==ElectroPneumatic)
//   Controlling->PipePress=0.5; //yB: w SPKS s� poprawnie zrobione pozycje
  if (Controlling->SlippingWheels)
  {
   Controlling->SandDoseOn(); //piasku!
   //Controlling->SlippingWheels=false; //a to tu nie ma sensu, flaga u�ywana w dalszej cz�ci
  }
 }
 //ABu-160305 testowanie gotowo�ci do jazdy
 //Ra: przeniesione z DynObj, sk�ad u�ytkownika te� jest testowany, �eby mu przekaza�, �e ma odhamowa�
 TDynamicObject *p=pVehicles[0]; //pojazd na czole sk�adu
 Ready=true; //wst�pnie gotowy
 fReady=0.0; //za�o�enie, �e odhamowany
 fAccGravity=0.0; //przyspieszenie wynikaj�ce z pochylenia
 double dy; //sk�adowa styczna grawitacji, w przedziale <0,1>
 while (p)
 {//sprawdzenie odhamowania wszystkich po��czonych pojazd�w
  if (Ready) //bo jak co� nie odhamowane, to dalej nie ma co sprawdza�
   //if (p->MoverParameters->BrakePress>=0.03*p->MoverParameters->MaxBrakePress)
   if (p->MoverParameters->BrakePress>=0.4) //wg UIC okre�lone sztywno na 0.04
   {Ready=false; //nie gotowy
    //Ra: odlu�nianie prze�adowanych lokomotyw, ci�gni�tych na zimno - prowizorka...
    if (AIControllFlag) //sk�ad jak dot�d by� wyluzowany
     if (Controlling->BrakeCtrlPos==0) //jest pozycja jazdy
      if (fabs(p->MoverParameters->PipePress-5.0)<0.005) //je�li ci�nienie jak dla jazdy
       if (p->MoverParameters->CntrlPipePress>p->MoverParameters->PipePress+0.1) //za du�o w zbiorniku
        p->MoverParameters->BrakeReleaser(); //indywidualne luzowanko
  }
  if (fReady<p->MoverParameters->BrakePress)
   fReady=p->MoverParameters->BrakePress; //szukanie najbardziej zahamowanego
  if ((dy=p->VectorFront().y)!=0.0) //istotne tylko dla pojazd�w na pochyleniu
   fAccGravity-=p->DirectionGet()*p->MoverParameters->TotalMassxg*dy; //ci�ar razy sk�adowa styczna grawitacji
  p=p->Next(); //pojazd pod��czony z ty�u (patrz�c od czo�a)
 }
 fAccGravity/=iDirection*fMass; //si�� generuj� pojazdy na pochyleniu ale dzia�a ona ca�o�� sk�adu, wi�c a=F/m
 if (!Ready) //v367: je�li wg powy�szych warunk�w sk�ad nie jest odhamowany
  if (fAccGravity<-0.05) //je�li ma pod g�r� na tyle, by si� stoczy�
   //if (Controlling->BrakePress<0.08) //to wystarczy, �e zadzia�aj� liniowe (nie ma ich jeszcze!!!)
   if (fReady<0.8) //delikatniejszy warunek, obejmuje wszystkie wagony
    Ready=true; //�eby uzna� za odhamowany
 HelpMeFlag=false;
 //Winger 020304
 if (AIControllFlag)
 {if (Controlling->Vel>0.0)
  {//je�eli jedzie
   //przy prowadzeniu samochodu trzeba ka�d� o� odsuwa� oddzielnie, inaczej kicha wychodzi
   if (Controlling->CategoryFlag&2) //je�li samoch�d
    //if (fabs(Controlling->OffsetTrackH)<Controlling->Dim.W) //Ra: szeroko�� drogi tu powinna by�?
     if (!Controlling->ChangeOffsetH(-0.01*Controlling->Vel*dt)) //ruch w poprzek drogi
      Controlling->ChangeOffsetH(0.01*Controlling->Vel*dt); //Ra: co to mia�o by�, to nie wiem
   if (Controlling->EnginePowerSource.SourceType==CurrentCollector)
   {Controlling->PantRear(true); //jazda na tylnym
    if (Controlling->Vel>30) //opuszczenie przedniego po rozp�dzeniu si�
     Controlling->PantFront(false);
    else //ewentualnie jak si� rozp�dza, to ustaw rozruch
     //if (fMass>800.0) //Ra: taki sobie warunek na wysoki rozruch
     // if (fAccGravity<-0.1) //jak pochylenie wi�ksze ni� 10�
     //  Controlling->CurrentSwitch(true); //rozruch wysoki (za to mo�e si� �lizga�)
     // else
       if (fAccGravity>-0.02) //jak pochylenie mnijsze ni� 2�
        Controlling->CurrentSwitch(false); //rozruch wysoki wy��cz
   }
  }
  if (iDrivigFlags&moveStartHornNow) //czy ma zatr�bi� przed ruszeniem?
   if (Ready) //got�w do jazdy
    if (EngineActive) //jeszcze si� odpali� musi
    {//uruchomienie tr�bienia
     fWarningDuration=0.3; //czas tr�bienia
     //if (AIControllFlag) //jak siedzi krasnoludek, to w��czy tr�bienie
     Controlling->WarningSignal=pVehicle->iHornWarning; //wysoko�� tonu (2=wysoki)
     iDrivigFlags|=moveStartHornDone; //nie tr�bi� a� do ruszenia
     iDrivigFlags&=~moveStartHornNow; //tr�bienie zosta�o zorganizowane
    }
 }
 ElapsedTime+=dt;
 WaitingTime+=dt;
 fBrakeTime-=dt; //wpisana warto�� jest zmniejszana do 0, gdy ujemna nale�y zmieni� nastaw� hamulca
 fStopTime+=dt; //zliczanie czasu postoju, nie ruszy dop�ki ujemne
 if (WriteLogFlag)
 {
  if (LastUpdatedTime>deltalog)
  {//zapis do pliku DAT
   PhysicsLog();
   if (fabs(Controlling->V)>0.1) //Ra: [m/s]
    deltalog=0.2;
   else deltalog=1.0;
   LastUpdatedTime=0.0;
  }
  else
   LastUpdatedTime=LastUpdatedTime+dt;
 }
 //Ra: skanowanie r�wnie� dla prowadzonego r�cznie, aby podpowiedzie� pr�dko��
 if ((LastReactionTime>Min0R(ReactionTime,2.0)))
 {
  //Ra: nie wiem czemu ReactionTime potrafi dosta� 12 sekund, to jest przegi�cie, bo prze�yna ST�J
  //yB: ot� jest to jedna trzecia czasu nape�niania na towarowym; mo�e si� przyda� przy wdra�aniu hamowania, �eby nie rusza�o kranem jak g�upie
  //Ra: ale nie mo�e si� budzi� co p� minuty, bo prze�yna semafory
  //Ra: trzeba by tak:
  // 1. Ustali� istotn� odleg�o�� zainteresowania (np. 3�droga hamowania z V.max).
  fBrakeDist=fDriverBraking*Controlling->Vel*(40.0+Controlling->Vel); //przybli�ona droga hamowania
  //double scanmax=(Controlling->Vel>0.0)?3*fDriverDist+fBrakeDist:10.0*fDriverDist;
  double scanmax=(Controlling->Vel>0.0)?150+fBrakeDist:20.0*fDriverDist; //1000m dla stoj�cych poci�g�w
  // 2. Sprawdzi�, czy tabelka pokrywa za�o�ony odcinek (nie musi, je�li jest STOP).
  // 3. Sprawdzi�, czy trajektoria ruchu przechodzi przez zwrotnice - je�li tak, to sprawdzi�, czy stan si� nie zmieni�.
  // 4. Ewentualnie uzupe�ni� tabelk� informacjami o sygna�ach i ograniczeniach, je�li si� "zu�y�a".
#if LOGVELOCITY
  WriteLog("");
  WriteLog("Scan table for "+pVehicle->asName+":");
#endif
  TableCheck(scanmax); //wype�nianie tabelki i aktualizacja odleg�o�ci
  // 5. Sprawdzi� stany sygnalizacji zapisanej w tabelce, wyznaczy� pr�dko�ci.
  // 6. Z tabelki wyznaczy� krytyczn� odleg�o�� i pr�dko�� (najmniejsze przyspieszenie).
  // 7. Je�li jest inny pojazd z przodu, ewentualnie skorygowa� odleg�o�� i pr�dko��.
  // 8. Ustali� cz�stotliwo�� �wiadomo�ci AI (zatrzymanie precyzyjne - cz�ciej, brak atrakcji - rzadziej).
  if (AIControllFlag)
  {//tu bedzie logika sterowania
   if (Controlling->CommandIn.Command!="")
    if (!Controlling->RunInternalCommand()) //rozpoznaj komende bo lokomotywa jej nie rozpoznaje
     RecognizeCommand(); //samo czyta komend� wstawion� do pojazdu?
   if (Controlling->SecuritySystem.Status>1)
    if (!Controlling->SecuritySystemReset())
     //if ((TestFlag(Controlling->SecuritySystem.Status,s_ebrake))&&(Controlling->BrakeCtrlPos==0)&&(AccDesired>0.0))
     if ((TestFlag(Controlling->SecuritySystem.Status,s_SHPebrake)||TestFlag(Controlling->SecuritySystem.Status,s_CAebrake))&&(Controlling->BrakeCtrlPos==0)&&(AccDesired>0.0))
      Controlling->DecBrakeLevel();
  }
  switch (OrderList[OrderPos])
  {//ustalenie pr�dko�ci przy doczepianiu i odczepianiu, dystans�w w pozosta�ych przypadkach
   case Connect: //pod��czanie do sk�adu
    if (iDrivigFlags&moveConnect)
    {//je�li stan�� ju� blisko, unikaj�c zderzenia i mo�na pr�bowa� pod��czy�
     fMinProximityDist=-0.01; fMaxProximityDist=0.0; //[m] dojecha� maksymalnie
     VelReduced=2; //[km/h]
     if (AIControllFlag)
     {//to robi tylko AI, wersj� dla cz�owieka trzeba dopiero zrobi�
      //sprz�gi sprawdzamy w pierwszej kolejno�ci, bo jak po��czony, to koniec
      bool ok; //true gdy si� pod��czy (uzyskany sprz�g b�dzie zgodny z ��danym)
      if (pVehicles[0]->DirectionGet()>0) //je�li sprz�g 0
      {//sprz�g 0 - pr�ba podczepienia
       if (pVehicles[0]->MoverParameters->Couplers[0].Connected) //je�li jest co� wykryte (a chyba jest, nie?)
        if (pVehicles[0]->MoverParameters->Attach(0,2,pVehicles[0]->MoverParameters->Couplers[0].Connected,iCoupler))
        {
         //dsbCouplerAttach->SetVolume(DSBVOLUME_MAX);
         //dsbCouplerAttach->Play(0,0,0);
        }
       //WriteLog("CoupleDist[0]="+AnsiString(pVehicles[0]->MoverParameters->Couplers[0].CoupleDist)+", Connected[0]="+AnsiString(pVehicles[0]->MoverParameters->Couplers[0].CouplingFlag));
       ok=(pVehicles[0]->MoverParameters->Couplers[0].CouplingFlag==iCoupler); //uda�o si�? (mog�o cz�ciowo)
      }
      else //if (pVehicles[0]->MoverParameters->DirAbsolute<0) //je�li sprz�g 1
      {//sprz�g 1 - pr�ba podczepienia
       if (pVehicles[0]->MoverParameters->Couplers[1].Connected) //je�li jest co� wykryte (a chyba jest, nie?)
        if (pVehicles[0]->MoverParameters->Attach(1,2,pVehicles[0]->MoverParameters->Couplers[1].Connected,iCoupler))
        {
         //dsbCouplerAttach->SetVolume(DSBVOLUME_MAX);
         //dsbCouplerAttach->Play(0,0,0);
        }
       //WriteLog("CoupleDist[1]="+AnsiString(Controlling->Couplers[1].CoupleDist)+", Connected[0]="+AnsiString(Controlling->Couplers[1].CouplingFlag));
       ok=(pVehicles[0]->MoverParameters->Couplers[1].CouplingFlag==iCoupler); //uda�o si�? (mog�o cz�ciowo)
      }
      if (ok)
      {//je�eli zosta� pod��czony
       iDrivigFlags&=~moveConnect; //zdj�cie flagi doczepiania
       SetVelocity(0,0,stopJoin); //wy��czy� przyspieszanie
       CheckVehicles(); //sprawdzi� �wiat�a nowego sk�adu
       JumpToNextOrder(); //wykonanie nast�pnej komendy
      }
      else
       SetVelocity(2.0,0.0); //jazda w ustawionym kierunku z pr�dko�ci� 2 (18s)
/*
      else if ((iDrivigFlags&moveConnect)==0) //je�li nie zbli�y� si� dostatecznie
       if (pVehicles[0]->fTrackBlock>100.0) //ta odleg�o�� mo�e by� rzadko od�wie�ana
       {SetVelocity(20.0,5.0); //jazda w ustawionym kierunku z pr�dko�ci� 20
        //SetProximityVelocity(-pVehicles[0]->fTrackBlock+50,0); //to nie dzia�a dobrze
        //SetProximityVelocity(pVehicles[0]->fTrackBlock,0); //to te� nie dzia�a dobrze
        //vCommandLocation=pVehicles[0]->AxlePositionGet()+pVehicles[0]->fTrackBlock*SafeNormalize(iDirection*pVehicles[0]->GetDirection());
        //WriteLog(AnsiString(vCommandLocation.x)+" "+AnsiString(vCommandLocation.z));
       }
      else
       {SetVelocity(2.0,0.0); //jazda w ustawionym kierunku z pr�dko�ci� 2 (18s)
        if (pVehicles[0]->fTrackBlock<2.0) //przy zderzeniu fTrackBlock nie jest miarodajne
         iDrivigFlags|=moveConnect; //wy��czenie sprawdzania fTrackBlock
       }
*/
     } //if (AIControllFlag) //koniec bloknawias, bo by�a zmienna lokalna
    }
    else
    {//jak daleko, to jazda jak dla Shunt na kolizj�
     fMinProximityDist=0.0; fMaxProximityDist=5.0; //[m] w takim przedziale odleg�o�ci powinien stan��
     VelReduced=5; //[km/h]
     //if (Controlling->Vel<0.5) //je�li ju� prawie stan��
      if (pVehicles[0]->fTrackBlock<=20.0) //przy zderzeniu fTrackBlock nie jest miarodajne
       iDrivigFlags|=moveConnect; //pocz�tek podczepiania, z wy��czeniem sprawdzania fTrackBlock
    }
   break;
   case Disconnect: //20.07.03 - manewrowanie wagonami
    fMinProximityDist=1.0; fMaxProximityDist=10.0; //[m]
    VelReduced=5; //[km/h]
    if (AIControllFlag)
    {if (iVehicleCount>=0) //je�li by�a podana ilo�� wagon�w
     {
      if (Prepare2press) //je�li dociskanie w celu odczepienia
      {//3. faza odczepiania.
       SetVelocity(2,0); //jazda w ustawionym kierunku z pr�dko�ci� 2
       if (Controlling->MainCtrlPos>0) //je�li jazda
       {
        //WriteLog("Odczepianie w kierunku "+AnsiString(Controlling->DirAbsolute));
        TDynamicObject *p=pVehicle; //pojazd do odczepienia, w (pVehicle) siedzi AI
        int d; //numer sprz�gu, kt�ry sprawdzamy albo odczepiamy
        int n=iVehicleCount; //ile wagon�w ma zosta�
        do
        {//przej�cie do nast�pnego pojazdu
         d=p->DirectionGet()>0?0:1; //numer sprz�gu od strony czo�a sk�adu
         //if (p->MoverParameters->Couplers[d].CouplerType==Articulated) //je�li sprz�g typu w�zek (za ma�o)
         if (p->MoverParameters->Couplers[d].CouplingFlag&ctrain_depot) //je�eli sprz�g zablokowany
          //if (p->GetTrack()->) //a nie stoi na torze warsztatowym (ustali� po czym pozna� taki tor)
           ++n; //to  liczymy cz�ony jako jeden
         p->MoverParameters->BrakeReleaser(); //wyluzuj pojazd, aby da�o si� dopycha�
         p->MoverParameters->DecBrakeLevel(); //nieczynne lokomotywy hamuj�
         if (n)
         {//je�li jeszcze nie koniec
          p=p->Prev(); //kolejny w stron� czo�a sk�adu (licz�c od ty�u), bo dociskamy
          if (!p) iVehicleCount=-2,n=0; //nie ma co dalej sprawdza�, doczepianie zako�czone
         }
        } while (n--);
        if (p)
         if (p->MoverParameters->Couplers[d].CouplingFlag>0)
          if (!p->Dettach(d)) //zwraca mask� bitow� po��czenia
          {//tylko je�li odepnie
           //WriteLog("Odczepiony od strony 0");
           iVehicleCount=-2;
          } //a jak nie, to dociska� dalej
       }
       if (iVehicleCount>=0) //zmieni si� po odczepieniu
        if (!Controlling->DecLocalBrakeLevel(1))
        {//doci�nij sklad
         //WriteLog("Dociskanie");
         //Controlling->BrakeReleaser(); //wyluzuj lokomotyw�
         //Ready=true; //zamiast sprawdzenia odhamowania ca�ego sk�adu
         IncSpeed(); //dla (Ready)==false nie ruszy
        }
      }
      if ((Controlling->Vel==0.0)&&!Prepare2press)
      {//2. faza odczepiania: zmie� kierunek na przeciwny i doci�nij
       //za rad� yB ustawiamy pozycj� 3 kranu (ruszanie kranem w innych miejscach powino zosta� wy��czone)
       //WriteLog("Zahamowanie sk�adu");
       //while ((Controlling->BrakeCtrlPos>3)&&Controlling->DecBrakeLevel());
       //while ((Controlling->BrakeCtrlPos<3)&&Controlling->IncBrakeLevel());
       Controlling->BrakeLevelSet(3);
       double p=Controlling->BrakePressureActual.PipePressureVal; //tu mo�e by� 0 albo -1 nawet
       if (p<3.7) p=3.7; //TODO: zabezpieczenie przed dziwnymi CHK do czasu wyja�nienia sensu 0 oraz -1 w tym miejscu
       if (Controlling->PipePress<p+0.1)
       {//je�li w miar� zosta� zahamowany (ci�nienie mniejsze ni� podane na pozycji 3, zwyle 0.37)
        //WriteLog("Luzowanie lokomotywy i zmiana kierunku");
        Controlling->BrakeReleaser(); //wyluzuj lokomotyw�; a ST45?
        Controlling->DecLocalBrakeLevel(10); //zwolnienie hamulca
        Prepare2press=true; //nast�pnie b�dzie dociskanie
        DirectionForward(Controlling->ActiveDir<0); //zmiana kierunku jazdy na przeciwny (dociskanie)
        CheckVehicles(); //od razu zmieni� �wiat�a (zgasi�)
        fStopTime=0.0; //nie ma na co czeka� z odczepianiem
       }
      }
     } //odczepiania
     else //to poni�ej je�li ilo�� wagon�w ujemna
      if (Prepare2press)
      {//4. faza odczepiania: zwolnij i zmie� kierunek
       SetVelocity(0,0,stopJoin); //wy��czy� przyspieszanie
       if (!DecSpeed()) //je�li ju� bardziej wy��czy� si� nie da
       {//ponowna zmiana kierunku
        //WriteLog("Ponowna zmiana kierunku");
        DirectionForward(Controlling->ActiveDir<0); //zmiana kierunku jazdy na w�a�ciwy
        Prepare2press=false; //koniec dociskania
        CheckVehicles(); //od razu zmieni� �wiat�a
        JumpToNextOrder();
        iDrivigFlags&=~moveStartHorn; //bez tr�bienia przed ruszeniem
        SetVelocity(fShuntVelocity,fShuntVelocity); //ustawienie pr�dko�ci jazdy
       }
      }
    }
   break;
   case Shunt:
    //na jaka odleglosc i z jaka predkoscia ma podjechac
    fMinProximityDist=2.0; fMaxProximityDist=4.0; //[m]
    VelReduced=5; //[km/h]
    break;
   case Obey_train:
    //na jaka odleglosc i z jaka predkoscia ma podjechac do przeszkody
    if (Controlling->CategoryFlag&1) //je�li poci�g
    {
     fMinProximityDist=10.0; fMaxProximityDist=20.0; //[m]
    }
    else //samochod (sokista te�)
    {
     fMinProximityDist=7.0; fMaxProximityDist=10.0; //[m]
    }
    VelReduced=4; //[km/h]
    break;
   default:
    fMinProximityDist=0.01; fMaxProximityDist=2.0; //[m]
    VelReduced=1; //[km/h]
  } //switch
  switch (OrderList[OrderPos])
  {//co robi maszynista
   case Prepare_engine: //odpala silnik
    //if (AIControllFlag)
    if (PrepareEngine()) //dla u�ytkownika tylko sprawdza, czy uruchomi�
    {//gotowy do drogi?
     SetDriverPsyche();
     //OrderList[OrderPos]:=Shunt; //Ra: to nie mo�e tak by�, bo scenerie robi� Jump_to_first_order i przechodzi w manewrowy
     JumpToNextOrder(); //w nast�pnym jest Shunt albo Obey_train, moze te� by� Change_direction, Connect albo Disconnect
     //if OrderList[OrderPos]<>Wait_for_Orders)
     // if BrakeSystem=Pneumatic)  //napelnianie uderzeniowe na wstepie
     //  if BrakeSubsystem=Oerlikon)
     //   if (BrakeCtrlPos=0))
     //    DecBrakeLevel;
    }
   break;
   case Release_engine:
    if (ReleaseEngine()) //zdana maszyna?
     JumpToNextOrder();
   break;
   case Jump_to_first_order:
    if (OrderPos>1)
     OrderPos=1; //w zerowym zawsze jest czekanie
    else
     ++OrderPos;
   break;
   case Wait_for_orders: //je�li czeka, te� ma skanowa�, �eby odpali� si� od semafora
/*
     if ((Controlling->ActiveDir!=0))
     {//je�li jest wybrany kierunek jazdy, mo�na ustali� pr�dko�� jazdy
      VelDesired=fVelMax; //wst�pnie pr�dko�� maksymalna dla pojazdu(-�w), b�dzie nast�pnie ograniczana
      SetDriverPsyche(); //ustawia AccPreferred (potrzebne tu?)
      //Ra: odczyt (ActualProximityDist), (VelNext) i (AccPreferred) z tabelki pr�dkosci
      AccDesired=AccPreferred; //AccPreferred wynika z osobowo�ci mechanika
      VelNext=VelDesired; //maksymalna pr�dko�� wynikaj�ca z innych czynnik�w ni� trajektoria ruchu
      ActualProximityDist=scanmax; //funkcja Update() mo�e pozostawi� warto�ci bez zmian
      //hm, kiedy� semafory wysy�a�y SetVelocity albo ShuntVelocity i ustaw�y tak VelActual - a teraz jak to zrobi�?
      TCommandType comm=TableUpdate(Controlling->Vel,VelDesired,ActualProximityDist,VelNext,AccDesired); //szukanie optymalnych warto�ci
     }
*/
   //break;
   case Shunt:
   case Obey_train:
   case Connect:
   case Disconnect:
   case Change_direction: //tryby wymagaj�ce jazdy
    if (OrderList[OrderPos]!=Obey_train) //spokojne manewry
    {
     VelActual=Min0R(VelActual,40); //je�li manewry, to ograniczamy pr�dko��
     if (AIControllFlag)
     {//to poni�ej tylko dla AI
      if (iVehicleCount>=0)
       if (!Prepare2press)
        if (Controlling->Vel>0.0)
         if (OrderList[OrderPos]!=Connect) //spokojne manewry
         {SetVelocity(0,0,stopJoin); //1. faza odczepiania: zatrzymanie
          //WriteLog("Zatrzymanie w celu odczepienia");
         }
     }   
    }
    else
     SetDriverPsyche(); //Ra: by�o w PrepareEngine(), potrzebne tu?
    //no albo przypisujemy -WaitingExpireTime, albo por�wnujemy z WaitingExpireTime
    //if ((VelActual==0.0)&&(WaitingTime>WaitingExpireTime)&&(Controlling->RunningTrack.Velmax!=0.0))
    if (OrderList[OrderPos]&(Shunt|Obey_train|Connect)) //odjecha� sam mo�e tylko je�li jest w trybie jazdy
    {//automatyczne ruszanie po odstaniu albo spod SBL
     if ((VelActual==0.0)&&(WaitingTime>0.0)&&(Controlling->RunningTrack.Velmax!=0.0))
     {//je�li stoi, a up�yn�� czas oczekiwania i tor ma niezerow� pr�dko��
/*
     if (WriteLogFlag)
      {
        append(AIlogFile);
        writeln(AILogFile,ElapsedTime:5:2,": ",Name," V=0 waiting time expired! (",WaitingTime:4:1,")");
        close(AILogFile);
      }
*/
      if ((OrderList[OrderPos]&(Obey_train|Shunt))?(iDrivigFlags&moveStopHere):false)
       WaitingTime=-WaitingExpireTime; //zakaz ruszania z miejsca bez otrzymania wolnej drogi
      else if (Controlling->CategoryFlag&1)
      {//je�li poci�g
       if (AIControllFlag)
       {PrepareEngine(); //zmieni ustawiony kierunek
        SetVelocity(20,20); //jak si� nasta�, to niech jedzie 20km/h
        WaitingTime=0.0;
        fWarningDuration=1.5; //a zatr�bi� troch�
        Controlling->WarningSignal=1;
       }
       else
        SetVelocity(20,20); //u�ytkownikowi zezwalamy jecha�
      }
      else
      {//samoch�d ma sta�, a� dostanie odjazd, chyba �e stoi przez kolizj�
       if (eStopReason==stopBlock)
        if (pVehicles[0]->fTrackBlock>fDriverDist)
         if (AIControllFlag)
         {PrepareEngine(); //zmieni ustawiony kierunek
          SetVelocity(-1,-1); //jak si� nasta�, to niech jedzie
          WaitingTime=0.0;
         }
         else
          SetVelocity(-1,-1); //u�ytkownikowi pozwalamy jecha� (samochodem?)
      }
     }
     else if ((VelActual==0.0)&&(VelNext>0.0)&&(Controlling->Vel<1.0))
      if ((OrderList[OrderPos]==Connect)?true:(iDrivigFlags&moveStopHere)==0) //Ra: tu jest co� nie tak, bo bez tego warunku rusza�o w manewrowym !!!!
       SetVelocity(VelNext,VelNext,stopSem); //omijanie SBL
    } //koniec samoistnego odje�d�ania
    if (AIControllFlag)
     if ((HelpMeFlag)||(Controlling->DamageFlag>0))
     {
      HelpMeFlag=false;
/*
      if (WriteLogFlag)
       with Controlling do
        {
          append(AIlogFile);
          writeln(AILogFile,ElapsedTime:5:2,": ",Name," HelpMe! (",DamageFlag,")");
          close(AILogFile);
        }
*/
     }
    if (AIControllFlag)
     if (OrderList[OrderPos]==Change_direction)
     {//sprobuj zmienic kierunek
      SetVelocity(0,0,stopDir); //najpierw trzeba si� zatrzyma�
      if (Controlling->Vel<0.1)
      {//je�li si� zatrzyma�, to zmieniamy kierunek jazdy, a nawet kabin�/cz�on
       Activation(); //ustawienie zadanego wcze�niej kierunku i ewentualne przemieszczenie AI
       PrepareEngine();
       JumpToNextOrder(); //nast�pnie robimy, co jest do zrobienia (Shunt albo Obey_train)
       if (OrderList[OrderPos]==Shunt) //je�li dalej mamy manewry
        if ((iDrivigFlags&moveStopHere)==0) //o ile nie sta� w miejscu
        {//jecha� od razu w przeciwn� stron� i nie tr�bi� z tego tytu�u
         iDrivigFlags&=~moveStartHorn; //bez tr�bienia przed ruszeniem
         SetVelocity(fShuntVelocity,fShuntVelocity); //to od razu jedziemy
        }
       //iDrivigFlags|=moveStartHorn; //a p�niej ju� mo�na tr�bi�
/*
       if (WriteLogFlag)
       {
        append(AIlogFile);
        writeln(AILogFile,ElapsedTime:5:2,": ",Name," Direction changed!");
        close(AILogFile);
       }
*/
      }
      //else
      // VelActual:=0.0; //na wszelki wypadek niech zahamuje
     } //Change_direction (tylko dla AI)
    //ustalanie zadanej predkosci
    if (EngineActive) //je�li ma silnik odpalony, to skanuje w poszukiwaniu sygna��w
    {//je�li jest wybrany kierunek jazdy, mo�na ustali� pr�dko�� jazdy
     //Ra: tu by jeszcze trzeba by�o wstawi� uzale�nienie (VelDesired) od odleg�o�ci od przeszkody
     // no chyba �eby to uwzgldni� ju� w (ActualProximityDist)
     VelDesired=fVelMax; //wst�pnie pr�dko�� maksymalna dla pojazdu(-�w), b�dzie nast�pnie ograniczana
     SetDriverPsyche(); //ustawia AccPreferred (potrzebne tu?)
     //Ra: odczyt (ActualProximityDist), (VelNext) i (AccPreferred) z tabelki pr�dkosci
     AccDesired=AccPreferred; //AccPreferred wynika z osobowo�ci mechanika
     VelNext=VelDesired; //maksymalna pr�dko�� wynikaj�ca z innych czynnik�w ni� trajektoria ruchu
     ActualProximityDist=scanmax; //funkcja Update() mo�e pozostawi� warto�ci bez zmian
     //hm, kiedy� semafory wysy�a�y SetVelocity albo ShuntVelocity i ustaw�y tak VelActual - a teraz jak to zrobi�?
     TCommandType comm=TableUpdate(VelDesired,ActualProximityDist,VelNext,AccDesired); //szukanie optymalnych warto�ci
     //if (VelActual!=VelDesired) //je�eli pr�dko�� zalecana jest inna (ale tryb te� mo�e by� inny)
     switch (comm)
     {//ustawienie VelActual - troch� proteza = do przemy�lenia
      case cm_SetVelocity: //od wersji 357 semafor nie budzi wy��czonej lokomotywy
       if (!(OrderList[OrderPos]&~(Obey_train|Shunt))) //jedzie w dowolnym trybie albo Wait_for_orders
        if (fabs(VelActual)>=1.0) //0.1 nie wysy�a si� do samochodow, bo potem nie rusz�
         PutCommand("SetVelocity",VelActual,VelNext,NULL); //komenda robi dodatkowe operacje
      break;
      case cm_ShuntVelocity: //od wersji 357 Tm nie budzi wy��czonej lokomotywy
       if (!(OrderList[OrderPos]&~(Obey_train|Shunt))) //jedzie w dowolnym trybie albo Wait_for_orders
        PutCommand("ShuntVelocity",VelActual,VelNext,NULL);
       else if (OrderList[OrderPos]&Connect) //je�li jest w trakcie ��czenia
        SetVelocity(VelActual,VelNext);
      break;
      case cm_Command: //komenda z kom�rki
       if (!(OrderList[OrderPos]&~(Obey_train|Shunt))) //jedzie w dowolnym trybie albo Wait_for_orders
        if (Controlling->Vel<0.1) //dopiero jak stanie
        {PutCommand(eSignNext->CommandGet(),eSignNext->ValueGet(1),eSignNext->ValueGet(2),NULL);
         eSignNext->StopCommandSent(); //si� wyknoa�o ju�
        }
      break;
     }
     if (VelNext==0.0)
      if (!(OrderList[OrderPos]&~(Shunt|Connect))) //jedzie w Shunt albo Connect, albo Wait_for_orders
      {//je�eli wolnej drogi nie ma, a jest w trybie manewrowym albo oczekiwania
       if (BackwardScan()) //je�li w drug� mo�na jecha�
       {//nale�y sprawdza� odleg�o�� od znalezionego sygnalizatora,
        //aby w przypadku pr�dko�ci 0.1 wyci�gn�� najpierw sk�ad za sygnalizator
        //i dopiero wtedy zmieni� kierunek jazdy, oczekuj�c podania pr�dko�ci >0.5
        iDirectionOrder=-iDirection; //zmiana kierunku jazdy
        OrderNext(Change_direction);
        OrderPush(Shunt);
       }
      }
     double vel=Controlling->Vel; //pr�dko�� w kierunku jazdy
     if (iDirection*Controlling->V<0) vel=-vel; //ujemna, gdy jedzie w przeciwn� stron�, ni� powinien
     if (VelDesired<0.0) VelDesired=fVelMax; //bo <0 w VelDesired nie mo�e by�
     //Ra: jazda na widoczno��
     if (pVehicles[0]->fTrackBlock<1000.0) //przy 300m sta� z zapami�tan� kolizj�
      pVehicles[0]->ABuScanObjects(pVehicles[0]->DirectionGet(),300.0); //skanowanie sprawdzaj�ce
     //if (Controlling->Vel>=0.1) //o ile jedziemy; jak stoimy to te� trzeba jako� zatrzymywa�
     if ((iDrivigFlags&moveConnect)==0) //przy ko�c�wce pod��czania nie hamowa�
     {//sprawdzenie jazdy na widoczno��
      TCoupling *c=pVehicles[0]->MoverParameters->Couplers+(pVehicles[0]->DirectionGet()>0?0:1); //sprz�g z przodu sk�adu
      if (c->Connected) //a mamy co� z przodu
       if (c->CouplingFlag==0) //je�li to co� jest pod��czone sprz�giem wirtualnym
       {//wyliczanie optymalnego przyspieszenia do jazdy na widoczno��
        double k=c->Connected->Vel; //pr�dko�� pojazdu z przodu (zak�adaj�c, �e jedzie w t� sam� stron�!!!)
        if (k<vel+10) //por�wnanie modu��w pr�dko�ci [km/h]
        {//zatroszczy� si� trzeba, je�li tamten nie jedzie znacz�co szybciej
         double d=pVehicles[0]->fTrackBlock-0.5*vel-fMaxProximityDist; //odleg�o�� bezpieczna zale�y od pr�dko�ci
         if (d<0) //je�li odleg�o�� jest zbyt ma�a
         {//AccPreferred=-0.9; //hamowanie maksymalne, bo jest za blisko
          if (k<10.0) //k - pr�dko�� tego z przodu
          {//je�li tamten porusza si� z niewielk� pr�dko�ci� albo stoi
           if (OrderCurrentGet()&Connect)
           {//je�li spinanie, to jecha� dalej
            AccPreferred=0.2; //nie hamuj
            VelNext=VelDesired=2.0; //i pakuj si� na tamtego
           }
           else //a normalnie to hamowa�
           {AccPreferred=-1.0; //to hamuj maksymalnie
            VelNext=VelDesired=0.0; //i nie pakuj si� na tamtego
           }
          }
          else //je�li oba jad�, to przyhamuj lekko i ogranicz pr�dko��
          {if (k<vel) //jak tamten jedzie wolniej
            if (d<fBrakeDist) //a jest w drodze hamowania
            {if (AccPreferred>-0.2) AccPreferred=-0.2; //to przyhamuj troszk�
             VelNext=VelDesired=int(k); //to chyba ju� sobie dohamuje wed�ug uznania
            }
          }
          ReactionTime=0.1; //orientuj si�, bo jest goraco
         }
         else
         {//je�li odleg�o�� jest wi�ksza, ustali� maksymalne mo�liwe przyspieszenie (hamowanie)
          k=(k*k-vel*vel)/(25.92*d); //energia kinetyczna dzielona przez mas� i drog� daje przyspieszenie
          if (k>0.0) k*=1.5; //jed� szybciej, je�li mo�esz
          //double ak=(c->Connected->V>0?1.0:-1.0)*c->Connected->AccS; //przyspieszenie tamtego
          if (d<fBrakeDist) //a jest w drodze hamowania
           if (k<AccPreferred)
           {//je�li nie ma innych powod�w do wolniejszej jazdy
            AccPreferred=k;
            if (VelNext>c->Connected->Vel)
            {VelNext=c->Connected->Vel; //ograniczenie do pr�dko�ci tamtego
             ActualProximityDist=d; //i odleg�o�� od tamtego jest istotniejsza
            }
            ReactionTime=0.2; //zwi�ksz czujno��
           }
#if LOGVELOCITY
          WriteLog("Collision: AccPreffered="+AnsiString(k));
#endif
         }
        }
       }
     }
     //sprawdzamy mo�liwe ograniczenia pr�dko�ci
     if (OrderCurrentGet()&(Shunt|Obey_train)) //w Connect nie, bo moveStopHere odnosi si� do stanu po po��czeniu
      if (iDrivigFlags&moveStopHere) //je�li ma czeka� na woln� drog�
       if (vel==0.0) //a stoi
        if (VelNext==0.0) //a wyjazdu nie ma
         VelDesired=0.0; //to ma sta�
     if (fStopTime<=0) //czas postoju przed dalsz� jazd� (np. na przystanku)
      VelDesired=0.0; //jak ma czeka�, to nie ma jazdy
     //else if (VelActual<0)
      //VelDesired=fVelMax; //ile fabryka dala (Ra: uwzgl�dione wagony)
     else
     // VelDesired=Min0R(fVelMax,VelActual); //VelActual>0 jest ograniczeniem pr�dko�ci (z r�nyc �r�de�)
      if (VelActual>=0)
       VelDesired=Min0R(VelDesired,VelActual);
     if (Controlling->RunningTrack.Velmax>=0) //ograniczenie pr�dko�ci z trajektorii ruchu
      VelDesired=Min0R(VelDesired,Controlling->RunningTrack.Velmax); //uwaga na ograniczenia szlakowej!
     if (VelforDriver>=0) //tu jest zero przy zmianie kierunku jazdy
      VelDesired=Min0R(VelDesired,VelforDriver); //Ra: tu mo�e by� 40, je�li mechanik nie ma znajomo�ci szlaaku, albo kierowca je�dzi 70
     if (TrainParams)
      if (TrainParams->CheckTrainLatency()<10.0)
       if (TrainParams->TTVmax>0.0)
        VelDesired=Min0R(VelDesired,TrainParams->TTVmax); //jesli nie spozniony to nie przekracza� rozkladowej
     AbsAccS=Controlling->AccS; //wypadkowa si� stycznych do toru
     if (Controlling->V<0.0) AbsAccS=-AbsAccS;
     //else if (Controlling->V==0.0) AbsAccS=0; //fabs(fAccGravity); //Ra: jesli sk�ad stoi, to dzia�a na niego sk�adowa styczna grawitacji
     if (vel<0) //je�eli si� stacza w ty�
      AbsAccS=-AbsAccS; //to przyspieszenie te� dzia�a wtedy w nieodpowiedni� stron�
     //AbsAccS+=fAccGravity; //wypadkowe przyspieszenie (czy to ma sens?)
#if LOGVELOCITY
     //WriteLog("VelDesired="+AnsiString(VelDesired)+", VelActual="+AnsiString(VelActual));
     WriteLog("Vel="+AnsiString(vel)+", AbsAccS="+AnsiString(AbsAccS)+", AccGrav="+AnsiString(fAccGravity));
#endif
     //ustalanie zadanego przyspieszenia
     //AccPreferred wynika z psychyki oraz uwzgl�nia mo�liwe zderzenie z pojazdem z przodu
     //AccDesired uwzgl�dnia sygna�y na drodze ruchu
     //if ((VelNext>=0.0)&&(ActualProximityDist>=0)&&(Controlling->Vel>=VelNext)) //gdy zbliza sie i jest za szybko do NOWEGO
     if ((VelNext>=0.0)&&(ActualProximityDist<=scanmax)&&(vel>=VelNext))
     {//gdy zbli�a si� i jest za szybki do nowej pr�dko�ci, albo stoi na zatrzymaniu
      if (vel>0.0)
      {//je�li jedzie
       if ((vel<VelReduced+VelNext)&&(ActualProximityDist>fMaxProximityDist*(1+0.1*vel))) //dojedz do semafora/przeszkody
       {//je�li jedzie wolniej ni� mo�na i jest wystarczaj�co daleko, to mo�na przyspieszy�
        if (AccPreferred>0.0) //je�li nie ma zawalidrogi
         AccDesired=0.5*AccPreferred;
         //VelDesired:=Min0R(VelDesired,VelReduced+VelNext);
       }
       else if (ActualProximityDist>fMinProximityDist)
       {//jedzie szybciej, ni� trzeba na ko�cu ActualProximityDist
        if (vel<VelNext+40.0) //dwustopniowe hamowanie - niski przy ma�ej r�nicy
        {//je�li jedzie wolniej ni� VelNext+35km/h //Ra: 40, �eby nie kombinowa�
         if (VelNext==0.0)
         {//je�li ma si� zatrzyma�, musi by� to robione precyzyjnie i skutecznie
          if (ActualProximityDist<fMaxProximityDist) //jak min�� ju� maksymalny dystans
          {//po prostu hamuj (niski stopie�) //ma stan��, a jest w drodze hamowania albo ma jecha�
           AccDesired=-0.2; //mo�na by precyzyjniej zatrzymywa�
           VelDesired=0.0; //Min0R(VelDesired,VelNext);
          }
          else if (ActualProximityDist>fBrakeDist)
          {//je�li ma stan��, a mie�ci si� w drodze hamowania
           if (vel<30.0)  //trzymaj 30 km/h
            AccDesired=Min0R(0.5*AccDesired,AccPreferred); //jak jest tu 0.5, to samochody si� dobijaj� do siebie
           else
            AccDesired=0.0;
          }
          else //25.92 (=3.6*3.6*2) - przelicznik z km/h na m/s
           AccDesired=-(vel*vel)/(25.92*(ActualProximityDist+0.1));//-fMinProximityDist));//-0.1; //mniejsze op�nienie przy ma�ej r�nicy
          ReactionTime=0.1; //i orientuj si� szybciej, jak masz stan��
         }
         else if (vel<VelNext+VelReduced) //je�li niewielkie przekroczenie
          AccDesired=0.0; //to olej (zacznij luzowa�)
         else
         {//je�li wi�ksze przekroczenie ni� VelReduced [km/h]
          AccDesired=(VelNext*VelNext-vel*vel)/(25.92*ActualProximityDist+0.1); //mniejsze op�nienie przy ma�ej r�nicy
          if (ActualProximityDist<fMaxProximityDist)
           ReactionTime=0.1; //i orientuj si� szybciej, je�li w krytycznym przedziale
         }
        }
        else //przy du�ej r�nicy wysoki stopie� (1,25 potrzebnego opoznienia)
         AccDesired=(VelNext*VelNext-vel*vel)/(20.73*ActualProximityDist+0.1); //najpierw hamuje mocniej, potem zluzuje
        if (AccPreferred<AccDesired)
         AccDesired=AccPreferred; //(1+abs(AccDesired))
        //ReactionTime=0.5*Controlling->BrakeDelay[2+2*Controlling->BrakeDelayFlag]; //aby szybkosc hamowania zalezala od przyspieszenia i opoznienia hamulcow
        //fBrakeTime=0.5*Controlling->BrakeDelay[2+2*Controlling->BrakeDelayFlag]; //aby szybkosc hamowania zalezala od przyspieszenia i opoznienia hamulcow
       }
       else
       {//jest bli�ej ni� fMinProximityDist
        VelDesired=Min0R(VelDesired,VelNext); //utrzymuj predkosc bo juz blisko
        ReactionTime=0.1; //i orientuj si� szybciej
       }
      }
      else  //zatrzymany (vel<=0.0)
       //if (iDrivigFlags&moveStopHere) //to nie dotyczy podczepiania
       //if ((VelNext>0.0)||(ActualProximityDist>fMaxProximityDist*1.2))
       if (VelNext>0.0)
        AccDesired=AccPreferred; //mo�na jecha�
       else //je�li daleko jecha� nie mo�na
        if (ActualProximityDist>fMaxProximityDist) //ale ma kawa�ek do sygnalizatora
        {//if ((iDrivigFlags&moveStopHere)?false:AccPreferred>0)
         if (AccPreferred>0)
          AccDesired=0.5*AccPreferred; //dociagnij do semafora;
         else
          VelDesired=0.0;//,AccDesired=-fabs(fAccGravity); //stoj (hamuj z si�� r�wn� sk�adowej stycznej grawitacji)
        }
        else
         VelDesired=0.0; //VelNext=0 i stoi bli�ej ni� fMaxProximityDist
     }
     else //gdy jedzie wolniej ni� potrzeba, albo nie ma przeszk�d na drodze
      AccDesired=(VelDesired!=0.0?AccPreferred:-0.01); //normalna jazda
     //koniec predkosci nastepnej
     if ((VelDesired>=0.0)&&(vel>VelDesired)) //jesli jedzie za szybko do AKTUALNEGO
      if (VelDesired==0.0) //jesli stoj, to hamuj, ale i tak juz za pozno :)
       AccDesired=-0.9; //hamuj solidnie
      else
       if ((vel<VelDesired+5)) //o 5 km/h to olej
       {if ((AccDesired>0.0))
         AccDesired=0.0;
       }
       else
        AccDesired=-0.2; //hamuj tak �rednio - to si� nie za�apywa�o na warunek <-0.2
     //koniec predkosci aktualnej
     if ((AccDesired>0.0)&&(VelNext>=0.0)) //wybieg b�d� lekkie hamowanie, warunki byly zamienione
      if (vel>VelNext+100.0) //lepiej zaczac hamowac
       AccDesired=-0.2;
      else
       if (vel>VelNext+70.0)
        AccDesired=0.0; //nie spiesz si�, bo b�dzie hamowanie
     //koniec wybiegu i hamowania
     if (AIControllFlag)
     {//cz�� wykonawcza tylko dla AI, dla cz�owieka jedynie napisy
      if (Controlling->ConvOvldFlag)
      {//wywali� bezpiecznik nadmiarowy przetwornicy
       while (DecSpeed()); //zerowanie nap�du
       Controlling->ConvOvldFlag=false; //reset nadmiarowego
       PrepareEngine(); //pr�ba ponownego za��czenia
      }
      //w��czanie bezpiecznika
      if (Controlling->EngineType==ElectricSeriesMotor)
       if (Controlling->FuseFlag||Need_TryAgain)
       {Need_TryAgain=false; //true, je�li druga pozycja w elektryku nie za�apa�a
        //if (!Controlling->DecScndCtrl(1)) //kr�cenie po ma�u
        // if (!Controlling->DecMainCtrl(1)) //nastawnik jazdy na 0
        Controlling->DecScndCtrl(2); //nastawnik bocznikowania na 0
        Controlling->DecMainCtrl(2); //nastawnik jazdy na 0
        if (!Controlling->FuseOn())
         HelpMeFlag=true;
        else
        {
         ++iDriverFailCount;
         if (iDriverFailCount>maxdriverfails)
          Psyche=Easyman;
         if (iDriverFailCount>maxdriverfails*2)
          SetDriverPsyche();
        }
       }
      if (Controlling->BrakeSystem==Pneumatic) //nape�nianie uderzeniowe
       if (Controlling->BrakeHandle==FV4a)
       {
        if (Controlling->BrakeCtrlPos==-2)
         Controlling->BrakeLevelSet(0);
//        if ((Controlling->BrakeCtrlPos<0)&&(Controlling->PipeBrakePress<0.01))//{(CntrlPipePress-(Volume/BrakeVVolume/10)<0.01)})
//         Controlling->IncBrakeLevel();
        if ((Controlling->BrakeCtrlPos==0)&&(AbsAccS<0.0)&&(AccDesired>0.0))
        //if FuzzyLogicAI(CntrlPipePress-PipePress,0.01,1))
//         if ((Controlling->BrakePress>0.5)&&(Controlling->LocalBrakePos<0.5))//{((Volume/BrakeVVolume/10)<0.485)})
         if ((Controlling->EqvtPipePress<4.95)&&(fReady>0.4))//{((Volume/BrakeVVolume/10)<0.485)})
          Controlling->DecBrakeLevel();
         else
          if (Need_BrakeRelease)
          {
           Need_BrakeRelease=false;
           Controlling->BrakeReleaser();
           //DecBrakeLevel(); //z tym by jeszcze mia�o jaki� sens
          }
//        if ((Controlling->BrakeCtrlPos<0)&&(Controlling->BrakePress<0.3))//{(CntrlPipePress-(Volume/BrakeVVolume/10)<0.01)})
        if ((Controlling->BrakeCtrlPos<0)&&((Controlling->EqvtPipePress>5.1)||(fReady<0.25)))//{(CntrlPipePress-(Volume/BrakeVVolume/10)<0.01)})
         Controlling->IncBrakeLevel();          
       }
#if LOGVELOCITY
      WriteLog("Dist="+FloatToStrF(ActualProximityDist,ffFixed,7,1)+", VelDesired="+FloatToStrF(VelDesired,ffFixed,7,1)+", AccDesired="+FloatToStrF(AccDesired,ffFixed,7,3)+", VelActual="+AnsiString(VelActual)+", VelNext="+AnsiString(VelNext));
#endif

      if (AccDesired>=0.0)
       if (Prepare2press)
        Controlling->BrakeReleaser(); //wyluzuj lokomotyw�
       else
        if (OrderList[OrderPos]!=Disconnect) //przy od��czaniu nie zwalniamy tu hamulca
         if ((AccDesired>0.0)||(fAccGravity*fAccGravity<0.001)) //luzuj tylko na plaskim lub przy ruszaniu
          while (DecBrake());  //je�li przyspieszamy, to nie hamujemy
      //Ra: zmieni�em 0.95 na 1.0 - trzeba ustali�, sk�d sie takie warto�ci bior�
      //margines dla pr�dko�ci jest doliczany tylko je�li oczekiwana pr�dko�� jest wi�ksza od 5km/h
      if ((fAccGravity<-0.01?AccDesired<-0.1:AbsAccS+fAccGravity>AccDesired)||(vel+(VelDesired>5.0?VelMargin:0.0)>VelDesired))
       if (!Prepare2press)
        while (DecSpeed()); //je�li hamujemy, to nie przyspieszamy
      //yB: usuni�te r�ne dziwne warunki, oddzielamy cz�� zadaj�c� od wykonawczej
      //zwiekszanie predkosci
      if (AbsAccS+fAccGravity<AccDesired) //je�li przyspieszenie pojazdu jest mniejsze ni� ��dane oraz
       if (vel<(VelDesired>5.0?VelDesired-5:VelDesired)) //je�li pr�dko�� w kierunku czo�a jest mniejsza ni� dozwolona
        IncSpeed();
      //if ((AbsAccS<AccDesired)&&(vel<VelDesired))
       //if (!MaxVelFlag) //Ra: to nie jest u�ywane
       //if (!IncSpeed()) //Ra: to tutaj jest bez sensu, bo nie doci�gnie do bezoporowej
       // MaxVelFlag=true; //Ra: to nie jest u�ywane
       //else
       // MaxVelFlag=false; //Ra: to nie jest u�ywane
      //if (Vel<VelDesired*0.85) and (AccDesired>0) and (EngineType=ElectricSeriesMotor)
      // and (RList[MainCtrlPos].R>0.0) and (not DelayCtrlFlag))
      // if (Im<Imin) and Ready=True {(BrakePress<0.01*MaxBrakePress)})
      //  IncMainCtrl(1); //zwieksz nastawnik skoro mo�esz - tak aby si� ustawic na bezoporowej

      //yB: usuni�te r�ne dziwne warunki, oddzielamy cz�� zadaj�c� od wykonawczej
      //zmniejszanie predkosci
      if (((fAccGravity<-0.05)&&(vel<0))||((AccDesired<fAccGravity-0.1)&&(AccDesired<AbsAccS+fAccGravity-0.05))) //u g�ry ustawia si� hamowanie na -0.2
      //if not MinVelFlag)
       if (fBrakeTime<0?true:(AccDesired<-0.3)||(Controlling->BrakeCtrlPos<=0))
        if (!IncBrake()) //je�li up�yn�� czas reakcji hamulca, chyba �e nag�e albo luzowa�
         MinVelFlag=true;
        else
        {MinVelFlag=false;
         fBrakeTime=3+0.5*(Controlling->BrakeDelay[2+2*Controlling->BrakeDelayFlag]-3);
         //Ra: ten czas nale�y zmniejszy�, je�li czas dojazdu do zatrzymania jest mniejszy
         fBrakeTime*=0.5; //Ra: tymczasowo, bo prze�yna S1
        }
      if ((AccDesired<fAccGravity-0.05)&&(AbsAccS+fAccGravity<AccDesired-0.2))
      //if ((AccDesired<0.0)&&(AbsAccS<AccDesired-0.1)) //ST44 nie hamuje na czas, 2-4km/h po mini�ciu tarczy
      {//jak hamuje, to nie tykaj kranu za cz�sto
       //yB: luzuje hamulec dopiero przy r�nicy op�nie� rz�du 0.2
       if (OrderList[OrderPos]!=Disconnect) //przy od��czaniu nie zwalniamy tu hamulca
        DecBrake(); //tutaj zmniejsza�o o 1 przy odczepianiu
       fBrakeTime=(Controlling->BrakeDelay[1+2*Controlling->BrakeDelayFlag])/3.0;
       fBrakeTime*=0.5; //Ra: tymczasowo, bo prze�yna S1
      }
      //Mietek-end1
      SpeedSet(); //ci�gla regulacja pr�dko�ci
#if LOGVELOCITY
      WriteLog("BrakePos="+AnsiString(Controlling->BrakeCtrlPos)+", MainCtrl="+AnsiString(Controlling->MainCtrlPos));
#endif

      //zapobieganie poslizgowi w czlonie silnikowym; Ra: Couplers[1] powinno by�
      if (Controlling->Couplers[0].Connected!=NULL)
       if (TestFlag(Controlling->Couplers[0].CouplingFlag,ctrain_controll))
        if (Controlling->Couplers[0].Connected->SlippingWheels)
         if (!Controlling->DecScndCtrl(1))
         {
          if (!Controlling->DecMainCtrl(1))
           if (Controlling->BrakeCtrlPos==Controlling->BrakeCtrlPosNo)
            Controlling->DecBrakeLevel();
          ++iDriverFailCount;
         }
      //zapobieganie poslizgowi u nas
      if (Controlling->SlippingWheels)
      {
       if (!Controlling->DecScndCtrl(2)) //bocznik na zero
        Controlling->DecMainCtrl(1);
       if (Controlling->BrakeCtrlPos==Controlling->BrakeCtrlPosNo)
        Controlling->DecBrakeLevel();
       else
        Controlling->AntiSlippingButton();
       ++iDriverFailCount;
       Controlling->SlippingWheels=false; //flaga ju� wykorzystana
      }
      if (iDriverFailCount>maxdriverfails)
      {
       Psyche=Easyman;
       if (iDriverFailCount>maxdriverfails*2)
        SetDriverPsyche();
      }
     } //if (AIControllFlag)
     else
     {//tu mozna da� komunikaty tekstowe albo s�owne: przyspiesz, hamuj (lekko, �rednio, mocno)
     }
    } //kierunek r�ny od zera
    if (AIControllFlag)
    {//odhamowywanie sk�adu po zatrzymaniu i zabezpieczanie lokomotywy
     if ((OrderList[OrderPos]&(Disconnect|Connect))==0) //przy (p)od��czaniu nie zwalniamy tu hamulca
      if ((Controlling->V==0.0)&&((VelDesired==0.0)||(AccDesired==0.0)))
       if ((Controlling->BrakeCtrlPos<1)||!Controlling->DecBrakeLevel())
        Controlling->IncLocalBrakeLevel(1); //dodatkowy na pozycj� 1
    }
    break; //rzeczy robione przy jezdzie
  } //switch (OrderList[OrderPos])
  //kasowanie licznika czasu
  LastReactionTime=0.0;
  //ewentualne skanowanie toru
  //if (!ScanMe)
  // ScanMe=true;
  UpdateOK=true;
 } //if ((LastReactionTime>Min0R(ReactionTime,2.0)))
 else
  LastReactionTime+=dt;
 if (AIControllFlag)
 {
  if (fWarningDuration>0.0) //je�li pozosta�o co� do wytr�bienia
  {//tr�bienie trwa nadal
   fWarningDuration=fWarningDuration-dt;
   if (fWarningDuration<0.05)
    Controlling->WarningSignal=0; //a tu si� ko�czy
   if (ReactionTime>fWarningDuration)
    ReactionTime=fWarningDuration; //wcze�niejszy przeb�ysk �wiadomo�ci, by zako�czy� tr�bienie
  }
  if (Controlling->Vel>=5.0) //jesli jedzie, mo�na odblokowa� tr�bienie, bo si� wtedy nie w��czy
  {iDrivigFlags&=~moveStartHornDone; //zatr�bi dopiero jak nast�pnym razem stanie
   iDrivigFlags|=moveStartHorn; //i tr�bi� przed nast�pnym ruszeniem
  }
  return UpdateOK;
 }
 else //if (AIControllFlag)
  return false; //AI nie obs�uguje
}

void __fastcall TController::JumpToNextOrder()
{
 if (OrderList[OrderPos]!=Wait_for_orders)
 {
  if (OrderPos<maxorders-1)
   ++OrderPos;
  else
   OrderPos=0;
 }
 OrderCheck();
#if LOGVELOCITY
 OrdersDump(); //normalnie nie ma po co tego wypisywa�
#endif
};

void __fastcall TController::JumpToFirstOrder()
{//taki relikt
 OrderPos=1;
 if (OrderTop==0) OrderTop=1;
 OrderCheck();
};

void __fastcall TController::OrderCheck()
{//reakcja na zmian� rozkazu
 if (OrderList[OrderPos]==Shunt)
  CheckVehicles();
 if (OrderList[OrderPos]==Obey_train)
 {CheckVehicles();
  iDrivigFlags|=moveStopPoint; //W4 s� widziane
 }
 else if (OrderList[OrderPos]==Change_direction)
  iDirectionOrder=-iDirection; //trzeba zmieni� jawnie, bo si� nie domy�li
 else if (OrderList[OrderPos]==Disconnect)
  iVehicleCount=iVehicleCount<0?0:iVehicleCount; //odczepianie lokomotywy
 else if (OrderList[OrderPos]==Wait_for_orders)
  OrdersClear(); //czyszczenie rozkaz�w i przeskok do zerowej pozycji
}

void __fastcall TController::OrderNext(TOrders NewOrder)
{//ustawienie rozkazu do wykonania jako nast�pny
 if (OrderList[OrderPos]==NewOrder)
  return; //je�li robi to, co trzeba, to koniec
 if (!OrderPos) OrderPos=1; //na pozycji zerowej pozostaje czekanie
 OrderTop=OrderPos; //ale mo�e jest czym� zaj�ty na razie
 if (NewOrder>=Shunt) //je�li ma jecha�
 {//ale mo�e by� zaj�ty chwilowymi operacjami
  while (OrderList[OrderTop]?OrderList[OrderTop]<Shunt:false) //je�li co� robi
   ++OrderTop; //pomijamy wszystkie tymczasowe prace
 }
 else
 {//je�li ma ustawion� jazd�, to wy��czamy na rzecz operacji
  while (OrderList[OrderTop]?(OrderList[OrderTop]<Shunt)&&(OrderList[OrderTop]!=NewOrder):false) //je�li co� robi
   ++OrderTop; //pomijamy wszystkie tymczasowe prace
 }
 OrderList[OrderTop++]=NewOrder; //dodanie rozkazu jako nast�pnego
}

void __fastcall TController::OrderPush(TOrders NewOrder)
{//zapisanie na stosie kolejnego rozkazu do wykonania
 if (OrderPos==OrderTop) //je�li mia�by by� zapis na aktalnej pozycji
  if (OrderList[OrderPos]<Shunt) //ale nie jedzie
   ++OrderTop; //niekt�re operacje musz� zosta� najpierw doko�czone => zapis na kolejnej
 if (OrderList[OrderTop]!=NewOrder) //je�li jest to samo, to nie dodajemy
  OrderList[OrderTop++]=NewOrder; //dodanie rozkazu na stos
 //if (OrderTop<OrderPos) OrderTop=OrderPos;
}

void __fastcall TController::OrdersDump()
{//wypisanie kolejnych rozkaz�w do logu
 WriteLog("Orders for "+pVehicle->asName+":");
 for (int b=0;b<maxorders;++b)
 {WriteLog(AnsiString(b)+": "+Order2Str(OrderList[b])+(OrderPos==b?" <-":""));
  if (b) //z wyj�tkiem pierwszej pozycji
   if (OrderList[b]==Wait_for_orders) //je�li ko�cowa komenda
    break; //dalej nie trzeba
 }
};

TOrders __fastcall TController::OrderCurrentGet()
{
 return OrderList[OrderPos];
}

TOrders __fastcall TController::OrderNextGet()
{
 return OrderList[OrderPos+1];
}

void __fastcall TController::OrdersInit(double fVel)
{//wype�nianie tabelki rozkaz�w na podstawie rozk�adu
 //ustawienie kolejno�ci komend, niezale�nie kto prowadzi
 //Mechanik->OrderPush(Wait_for_orders); //czekanie na lepsze czasy
 //OrderPos=OrderTop=0; //wype�niamy od pozycji 0
 OrdersClear(); //usuni�cie poprzedniej tabeli
 OrderPush(Prepare_engine); //najpierw odpalenie silnika
 if (TrainParams->TrainName==AnsiString("none"))
 {//brak rozk�adu to jazda manewrowa
  if (fVel>0.05) //typowo 0.1 oznacza gotowo�� do jazdy, 0.01 tylko za��czenie silnika
   OrderPush(Shunt); //je�li nie ma rozk�adu, to manewruje
 }
 else
 {//je�li z rozk�adem, to jedzie na szlak
  if (TrainParams?
   (TrainParams->TimeTable[1].StationWare.Pos("@")? //je�li obr�t na pierwszym przystanku
   (Controlling->TrainType&(dt_EZT)? //SZT r�wnie�! SN61 zale�nie od wagon�w...
   (TrainParams->TimeTable[1].StationName==TrainParams->Relation1):false):false):true)
   OrderPush(Shunt); //a teraz start b�dzie w manewrowym, a tryb poci�gowy w��czy W4
  else
  //je�li start z pierwszej stacji i jednocze�nie jest na niej zmiana kierunku, to EZT ma mie� Shunt
   OrderPush(Obey_train); //dla starych scenerii start w trybie pociagowym
  if (DebugModeFlag) //normalnie nie ma po co tego wypisywa�
   WriteLog("/* Timetable: "+TrainParams->ShowRelation());
  TMTableLine *t;
  for (int i=0;i<=TrainParams->StationCount;++i)
  {t=TrainParams->TimeTable+i;
   if (DebugModeFlag) //normalnie nie ma po co tego wypisywa�
    WriteLog(AnsiString(t->StationName)+" "+AnsiString((int)t->Ah)+":"+AnsiString((int)t->Am)+", "+AnsiString((int)t->Dh)+":"+AnsiString((int)t->Dm)+" "+AnsiString(t->StationWare));
   if (AnsiString(t->StationWare).Pos("@"))
   {//zmiana kierunku i dalsza jazda wg rozk�adu
    if (Controlling->TrainType&(dt_EZT)) //SZT r�wnie�! SN61 zale�nie od wagon�w...
    {//je�li sk�ad zespolony, wystarczy zmieni� kierunek jazdy
     OrderPush(Change_direction); //zmiana kierunku
    }
    else
    {//dla zwyk�ego sk�adu wagonowego odczepiamy lokomotyw�
     OrderPush(Disconnect); //odczepienie lokomotywy
     OrderPush(Shunt); //a dalej manewry
     if (i<=TrainParams->StationCount)
     {//"@" na ostatniej robi tylko odpi�cie
      OrderPush(Change_direction); //zmiana kierunku
      OrderPush(Shunt); //jazda na drug� stron� sk�adu
      OrderPush(Change_direction); //zmiana kierunku
      OrderPush(Connect); //jazda pod wagony
     }
    }
    if (i<TrainParams->StationCount) //jak nie ostatnia stacja
     OrderPush(Obey_train); //to dalej wg rozk�adu
   }
  }
  if (DebugModeFlag) //normalnie nie ma po co tego wypisywa�
   WriteLog("*/");
  OrderPush(Shunt); //po wykonaniu rozk�adu prze��czy si� na manewry
 }
 //McZapkie-100302 - to ma byc wyzwalane ze scenerii
 if (fVel==0.0)
  SetVelocity(0,0,stopSleep); //je�li nie ma pr�dko�ci pocz�tkowej, to �pi
 else
 {//je�li podana niezerowa pr�dko��
  if ((fVel>=1.0)||(fVel<0)) //je�li ma jecha�
   iDrivigFlags=(iDrivigFlags&~moveStopHere)|moveStopCloser; //to do nast�pnego W4 ma podjecha� blisko
  else
   iDrivigFlags|=moveStopHere; //czeka� na sygna�
  JumpToFirstOrder();
  if (fVel>=1.0) //je�li ma jecha�
   SetVelocity(fVel,-1); //ma ustawi� ��dan� pr�dko��
  else
   SetVelocity(0,0,stopSleep); //pr�dko�� w przedziale (0;1) oznacza, �e ma sta�
 }
 if (DebugModeFlag) //normalnie nie ma po co tego wypisywa�
  OrdersDump(); //wypisanie kontrolne tabelki rozkaz�w
 //McZapkie! - zeby w ogole AI ruszyl to musi wykonac powyzsze rozkazy
 //Ale mozna by je zapodac ze scenerii
};


AnsiString __fastcall TController::StopReasonText()
{//informacja tekstowa o przyczynie zatrzymania
 if (eStopReason!=7)
  return StopReasonTable[eStopReason];
 else
  return "Blocked by "+(pVehicles[0]->PrevAny()->GetName());
};

//----------------------------------------------------------------------------------------------------------------------
//McZapkie: skanowanie semafor�w
//Ra: stare funkcje skanuj�ce, u�ywane podczas manewr�w do szukania sygnalizatora z ty�u
//- nie reaguj� na PutValues, bo nie ma takiej potrzeby
//- rozpoznaj� tylko zerow� pr�dko�� (jako koniec toru i brak podstaw do dalszego skanowania)
//----------------------------------------------------------------------------------------------------------------------

/* //nie u�ywane
double __fastcall TController::Distance(vector3 &p1,vector3 &n,vector3 &p2)
{//Ra:obliczenie odleg�o�ci punktu (p1) od p�aszczyzny o wektorze normalnym (n) przechodz�cej przez (p2)
 return n.x*(p1.x-p2.x)+n.y*(p1.y-p2.y)+n.z*(p1.z-p2.z); //ax1+by1+cz1+d, gdzie d=-(ax2+by2+cz2)
};
*/

TEvent* __fastcall TController::CheckTrackEventBackward(double fDirection,TTrack *Track)
{//sprawdzanie eventu w torze, czy jest sygna�owym - skanowanie do ty�u
 TEvent* e=(fDirection>0)?Track->Event2:Track->Event1;
 if (e)
  if (!e->bEnabled) //je�li sygna�owy (nie dodawany do kolejki)
   if (e->Type==tp_GetValues) //PutValues nie mo�e si� zmieni�
    return e;
 return NULL;
}

TTrack* __fastcall TController::BackwardTraceRoute(double &fDistance,double &fDirection,TTrack *Track,TEvent*&Event)
{//szukanie sygnalizatora w kierunku przeciwnym jazdy (eventu odczytu kom�rki pami�ci)
 //Ra: funkcja aktualnie nie u�ywana, przerania na skanowanie wstecz
 TTrack *pTrackChVel=Track; //tor ze zmian� pr�dko�ci
 TTrack *pTrackFrom; //odcinek poprzedni, do znajdywania ko�ca dr�g
 double fDistChVel=-1; //odleg�o�� do toru ze zmian� pr�dko�ci
 double fCurrentDistance=pVehicle->RaTranslationGet(); //aktualna pozycja na torze
 double s=0;
 if (fDirection>0) //je�li w kierunku Point2 toru
  fCurrentDistance=Track->Length()-fCurrentDistance;
 if ((Event=CheckTrackEventBackward(fDirection,Track))!=NULL)
 {//je�li jest semafor na tym torze
  fDistance=0; //to na tym torze stoimy
  return Track;
 }
 if ((Track->VelocityGet()==0.0)||(Track->iDamageFlag&128))
 {//jak pr�dkos� 0 albo uszkadza, to nie ma po co skanowa�
  fDistance=0; //to na tym torze stoimy
  return NULL; //stop, skanowanie nie da�o sensownych rezultat�w
 }
 while (s<fDistance)
 {
  //Track->ScannedFlag=true; //do pokazywania przeskanowanych tor�w
  pTrackFrom=Track; //zapami�tanie aktualnego odcinka
  s+=fCurrentDistance; //doliczenie kolejnego odcinka do przeskanowanej d�ugo�ci
  if (fDirection>0)
  {//je�li szukanie od Point1 w kierunku Point2
   if (Track->iNextDirection)
    fDirection=-fDirection;
   Track=Track->CurrentNext(); //mo�e by� NULL
  }
  else //if (fDirection<0)
  {//je�li szukanie od Point2 w kierunku Point1
   if (!Track->iPrevDirection)
    fDirection=-fDirection;
   Track=Track->CurrentPrev(); //mo�e by� NULL
  }
  if (Track==pTrackFrom) Track=NULL; //koniec, tak jak dla tor�w
  if (Track?(Track->VelocityGet()==0.0)||(Track->iDamageFlag&128):true)
  {//gdy dalej toru nie ma albo zerowa pr�dko��, albo uszkadza pojazd
   fDistance=s;
   return NULL; //zwraca NULL, �e skanowanie nie da�o sensownych rezultat�w
  }
  fCurrentDistance=Track->Length();
  if ((Event=CheckTrackEventBackward(fDirection,Track))!=NULL)
  {//znaleziony tor z eventem
   fDistance=s;
   return Track;
  }
 }
 Event=NULL; //jak dojdzie tu, to nie ma semafora
 if (fDistChVel<0)
 {//zwraca ostatni sprawdzony tor
  fDistance=s;
  return Track;
 }
 fDistance=fDistChVel; //odleg�o�� do zmiany pr�dko�ci
 return pTrackChVel; //i tor na kt�rym si� zmienia
}


//sprawdzanie zdarze� semafor�w i ogranicze� szlakowych
void __fastcall TController::SetProximityVelocity(double dist,double vel,const vector3 *pos)
{//Ra:przeslanie do AI pr�dko�ci
/*
 //!!!! zast�pi� prawid�ow� reakcj� AI na SetProximityVelocity !!!!
 if (vel==0)
 {//je�li zatrzymanie, to zmniejszamy dystans o 10m
  dist-=10.0;
 };
 if (dist<0.0) dist=0.0;
 if ((vel<0)?true:dist>0.1*(MoverParameters->Vel*MoverParameters->Vel-vel*vel)+50)
 {//je�li jest dalej od umownej drogi hamowania
*/
  PutCommand("SetProximityVelocity",dist,vel,pos);
#if LOGVELOCITY
  WriteLog("-> SetProximityVelocity "+AnsiString(floor(dist))+" "+AnsiString(vel));
#endif
/*
 }
 else
 {//je�li jest zagro�enie, �e przekroczy
  Mechanik->SetVelocity(floor(0.2*sqrt(dist)+vel),vel,stopError);
#if LOGVELOCITY
  WriteLog("-> SetVelocity "+AnsiString(floor(0.2*sqrt(dist)+vel))+" "+AnsiString(vel));
#endif
 }
 */
}

bool __fastcall TController::BackwardScan()
{//sprawdzanie zdarze� semafor�w z ty�u pojazdu
 //dzi�ki temu b�dzie mo�na stawa� za wskazanym sygnalizatorem, a zw�aszcza je�li b�dzie jazda na kozio�
 //ograniczenia pr�dko�ci nie s� wtedy istotne, r�wnie� koniec toru jest do niczego nie przydatny
 //zwraca true, je�li nale�y odwr�ci� pojazd
 if ((OrderList[OrderPos]&~(Shunt|Connect)))
  return false; //skanowanie sygna��w tylko gdy jedzie w trybie manewrowym albo czeka na rozkazy
 vector3 sl;
 int startdir=-pVehicles[0]->DirectionGet(); //kierunek jazdy wzgl�dem sprz�g�w pojazdu na czele
 if (startdir==0) //je�li kabina i kierunek nie jest okre�lony
  return false; //nie robimy nic
 double scandir=startdir*pVehicles[0]->RaDirectionGet(); //szukamy od pierwszej osi w wybranym kierunku
 if (scandir!=0.0) //skanowanie toru w poszukiwaniu event�w GetValues (PutValues nie s� przydatne)
 {//Ra: skanowanie drogi proporcjonalnej do kwadratu aktualnej pr�dko�ci (+150m), no chyba �e stoi (wtedy 1000m)
  double scanmax=1000; //1000m do ty�u, �eby widzia� przeciwny koniec stacji
  double scandist=scanmax; //zmodyfikuje na rzeczywi�cie przeskanowane
  TEvent *e=NULL; //event potencjalnie od semafora
  TTrack *scantrack=BackwardTraceRoute(scandist,scandir,pVehicles[0]->RaTrackGet(),e); //wg drugiej osi w kierunku ruchu
  vector3 dir=startdir*pVehicles[0]->VectorFront(); //wektor w kierunku jazdy/szukania
  if (!scantrack) //je�li wykryto koniec toru
   return false; //to raczej nic si� nie da z t� wiadomo�ci� zrobi�
  else
  {//je�li s� dalej tory
   double vmechmax; //pr�dko�� ustawiona semaforem
   if (e)
   {//je�li jest jaki� sygna� na widoku
#if LOGBACKSCAN
    AnsiString edir=pVehicle->asName+" - "+AnsiString((scandir>0)?"Event2 ":"Event1 ");
#endif
    //najpierw sprawdzamy, czy semafor czy inny znak zosta� przejechany
    vector3 pos=pVehicles[1]->RearPosition();//pozycja ty�u
    vector3 sem; //wektor do sygna�u
    if (e->Type==tp_GetValues)
    {//przes�a� info o zbli�aj�cym si� semaforze
#if LOGBACKSCAN
     edir+="("+(e->Params[8].asGroundNode->asName)+"): ";
#endif
     //sem=*e->PositionGet()-pos; //wektor do kom�rki pami�ci
     sem=e->Params[8].asGroundNode->pCenter-pos; //wektor do kom�rki pami�ci
     if (dir.x*sem.x+dir.z*sem.z<0) //je�li zosta� mini�ty
      if ((Controlling->CategoryFlag&1)?(VelNext!=0.0):true) //dla poci�gu wymagany sygna� zezwalaj�cy
      {//iloczyn skalarny jest ujemny, gdy sygna� stoi z ty�u
#if LOGBACKSCAN
       WriteLog(edir+"- will be ignored as passed by");
#endif
       return false;
      }
     sl=e->Params[8].asGroundNode->pCenter;
     vmechmax=e->ValueGet(1); //pr�dko�� przy tym semaforze
     //przeliczamy odleg�o�� od semafora - potrzebne by by�y wsp�rz�dne pocz�tku sk�adu
     //scandist=(pos-e->Params[8].asGroundNode->pCenter).Length()-0.5*Controlling->Dim.L-10; //10m luzu
     scandist=(pos-e->Params[8].asGroundNode->pCenter).Length()-10; //10m luzu
     if (scandist<0) scandist=0; //ujemnych nie ma po co wysy�a�
     bool move=false; //czy AI w trybie manewerowym ma doci�gn�� pod S1
     if (e->CommandGet()=="SetVelocity")
      if ((vmechmax==0.0)?(OrderCurrentGet()==Shunt):false)
       move=true; //AI w trybie manewerowym ma doci�gn�� pod S1
      else
      {//
       if ((scandist>fMinProximityDist)?(Controlling->Vel>0.0)&&(OrderCurrentGet()!=Shunt):false)
       {//je�li semafor jest daleko, a pojazd jedzie, to informujemy o zmianie pr�dko�ci
        //je�li jedzie manewrowo, musi dosta� SetVelocity, �eby sie na poci�gowy prze��czy�
        //Mechanik->PutCommand("SetProximityVelocity",scandist,vmechmax,sl);
#if LOGBACKSCAN
        //WriteLog(edir+"SetProximityVelocity "+AnsiString(scandist)+" "+AnsiString(vmechmax));
        WriteLog(edir);
#endif
        //SetProximityVelocity(scandist,vmechmax,&sl);
        return (vmechmax>0);
       }
       else  //ustawiamy pr�dko�� tylko wtedy, gdy ma ruszy�, stan�� albo ma sta�
        //if ((MoverParameters->Vel==0.0)||(vmechmax==0.0)) //je�li stoi lub ma stan��/sta�
        {//semafor na tym torze albo lokomtywa stoi, a ma ruszy�, albo ma stan��, albo nie rusza�
         //stop trzeba powtarza�, bo inaczej zatr�bi i pojedzie sam
         //PutCommand("SetVelocity",vmechmax,e->Params[9].asMemCell->Value2(),&sl,stopSem);
#if LOGBACKSCAN
         WriteLog(edir+"SetVelocity "+AnsiString(vmechmax)+" "+AnsiString(e->Params[9].asMemCell->Value2()));
#endif
         return (vmechmax>0);
        }
      }
     if (OrderCurrentGet()?OrderCurrentGet()==Shunt:true) //w Wait_for_orders te� widzi tarcze
     {//reakcja AI w trybie manewrowym dodatkowo na sygna�y manewrowe
      if (move?true:e->CommandGet()=="ShuntVelocity")
      {//je�li powy�ej by�o SetVelocity 0 0, to doci�gamy pod S1
       if ((scandist>fMinProximityDist)?(Controlling->Vel>0.0)||(vmechmax==0.0):false)
       {//je�li tarcza jest daleko, to:
        //- jesli pojazd jedzie, to informujemy o zmianie pr�dko�ci
        //- je�li stoi, to z w�asnej inicjatywy mo�e podjecha� pod zamkni�t� tarcz�
        if (Controlling->Vel>0.0) //tylko je�li jedzie
        {//Mechanik->PutCommand("SetProximityVelocity",scandist,vmechmax,sl);
#if LOGBACKSCAN
         //WriteLog(edir+"SetProximityVelocity "+AnsiString(scandist)+" "+AnsiString(vmechmax));
         WriteLog(edir);
#endif
         //SetProximityVelocity(scandist,vmechmax,&sl);
         return false;
        }
       }
       else //ustawiamy pr�dko�� tylko wtedy, gdy ma ruszy�, albo stan�� albo ma sta� pod tarcz�
       {//stop trzeba powtarza�, bo inaczej zatr�bi i pojedzie sam
        //if ((MoverParameters->Vel==0.0)||(vmechmax==0.0)) //je�li jedzie lub ma stan��/sta�
        {//nie dostanie komendy je�li jedzie i ma jecha�
         //PutCommand("ShuntVelocity",vmechmax,e->Params[9].asMemCell->Value2(),&sl,stopSem);
#if LOGBACKSCAN
         WriteLog(edir+"ShuntVelocity "+AnsiString(vmechmax)+" "+AnsiString(e->ValueGet(2)));
#endif
         return (vmechmax>0);
        }
       }
       if ((vmechmax!=0.0)&&(scandist<100.0))
       {//je�li Tm w odleg�o�ci do 100m podaje zezwolenie na jazd�, to od razu j� ignorujemy, aby m�c szuka� kolejnej
        //eSignSkip=e; //wtedy uznajemy ignorowan� przy poszukiwaniu nowej
#if LOGBACKSCAN
        WriteLog(edir+"- will be ignored due to Ms2");
#endif
        return (vmechmax>0);
       }
      } //if (move?...
     } //if (OrderCurrentGet()==Shunt)
     if (!e->bEnabled) //je�li skanowany
      if (e->StopCommand()) //a pod��czona kom�rka ma komend�
       return true; //to te� si� obr�ci�
    } //if (e->Type==tp_GetValues)
   } //if (e)
  } //if (scantrack)
 } //if (scandir!=0.0)
 return false;
};

AnsiString __fastcall TController::NextStop()
{//informacja o nast�pnym zatrzymaniu, wy�wietlane pod [F1]
 if (asNextStop.Length()<20) return ""; //nie zawiera nazwy stacji, gdy dojecha� do ko�ca
 //doda� godzin� odjazdu
 if (!TrainParams)
  return ""; //tu nie powinno nigdy wej��
 TMTableLine *t=TrainParams->TimeTable+TrainParams->StationIndex;
 if (t->Dh>=0) //je�li jest godzina odjazdu
  return
   asNextStop.SubString(20,30)
    +AnsiString(" ")
    +AnsiString(int(t->Dh))
    +AnsiString(":")
    +AnsiString(int(100+t->Dm)).SubString(2,2); //odjazd
 else if (t->Ah>=0) //przyjazd
  return
   asNextStop.SubString(20,30)
    +AnsiString(" (")
    +AnsiString(int(t->Ah))
    +AnsiString(":")
    +AnsiString(int(100+t->Am)).SubString(2,2)
    +AnsiString(")"); //przyjazd
 return "";
};

//-----------koniec skanowania semaforow

void __fastcall TController::TakeControl(bool yes)
{//przej�cie kontroli przez AI albo oddanie
 if (AIControllFlag==yes) return; //ju� jest jak ma by�
 if (yes) //�eby nie wykonywa� dwa razy
 {//teraz AI prowadzi
  AIControllFlag=AIdriver;
  pVehicle->Controller=AIdriver;
  //gdy zgaszone �wiat�a, flaga podje�d�ania pod semafory pozostaje bez zmiany
  if (!OrderCurrentGet()) //je�li nic nie robi
   if (pVehicle->iLights[Controlling->CabNo<0?1:0]&21) //kt�re� ze �wiate� zapalone?
   {//od wersji 357 oczekujemy podania komend dla AI przez sceneri�
    OrderNext(Prepare_engine);
    if (pVehicle->iLights[Controlling->CabNo<0?1:0]&4) //g�rne �wiat�o zapalone
     OrderNext(Obey_train); //jazda poci�gowa
    else
     OrderNext(Shunt); //jazda manewrowa
    if (Controlling->Vel>1.0) //je�li jedzie (dla 0.1 ma sta�) 
     iDrivigFlags&=~moveStopHere; //to ma nie czeka� na sygna�, tylko jecha�
    else
     iDrivigFlags|=moveStopHere; //a jak stoi, to niech czeka
   }
/* od wersji 357 oczekujemy podania komend dla AI przez sceneri�
  if (OrderCurrentGet())
  {if (OrderCurrentGet()<Shunt)
   {OrderNext(Prepare_engine);
    if (pVehicle->iLights[Controlling->CabNo<0?1:0]&4) //g�rne �wiat�o
     OrderNext(Obey_train); //jazda poci�gowa
    else
     OrderNext(Shunt); //jazda manewrowa
   }
  }
  else //je�li jest w stanie Wait_for_orders
   JumpToFirstOrder(); //uruchomienie?
  // czy dac ponizsze? to problematyczne
  //SetVelocity(pVehicle->GetVelocity(),-1); //utrzymanie dotychczasowej?
  if (pVehicle->GetVelocity()>0.0)
   SetVelocity(-1,-1); //AI ustali sobie odpowiedni� pr�dko��
*/
  CheckVehicles(); //ustawienie �wiate�
  TableClear(); //ponowne utworzenie tabelki, bo cz�owiek m�g� pojecha� niezgodnie z sygna�ami
 }
 else
 {//a teraz u�ytkownik
  AIControllFlag=Humandriver;
  pVehicle->Controller=Humandriver;
 }
};

void __fastcall TController::DirectionForward(bool forward)
{//ustawienie jazdy do przodu dla true i do ty�u dla false (zale�y od kabiny)
 while (DecSpeed()); //wy��czenie jazdy, inaczej si� mo�e zawiesi�
 if (forward)
  while (Controlling->ActiveDir<=0)
   Controlling->DirectionForward(); //do przodu w obecnej kabinie
 else
  while (Controlling->ActiveDir>=0)
   Controlling->DirectionBackward(); //do ty�u w obecnej kabinie
};

AnsiString __fastcall TController::Relation()
{//zwraca relacj� poci�gu
 return TrainParams->ShowRelation();
};

AnsiString __fastcall TController::TrainName()
{//zwraca relacj� poci�gu
 return TrainParams->TrainName;
};

int __fastcall TController::StationCount()
{//zwraca ilo�� stacji (miejsc zatrzymania)
 return TrainParams->StationCount;
};

int __fastcall TController::StationIndex()
{//zwraca indeks aktualnej stacji (miejsca zatrzymania)
 return TrainParams->StationIndex;
};

bool __fastcall TController::IsStop()
{//informuje, czy jest zatrzymanie na najbli�szej stacji
 return TrainParams->IsStop();
};
