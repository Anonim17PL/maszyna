/*
This Source Code Form is subject to the
terms of the Mozilla Public License, v.
2.0. If a copy of the MPL was not
distributed with this file, You can
obtain one at
http://mozilla.org/MPL/2.0/.
*/
/*
    MaSzyna EU07 locomotive simulator
    Copyright (C) 2001-2004  Marcin Wozniak, Maciej Czapkiewicz and others

*/

#include "stdafx.h"
#include "Globals.h"
#include "usefull.h"
//#include "Mover.h"
#include "Driver.h"
#include "Console.h"
#include "World.h"
#include "parser.h"
#include "Logs.h"
#include "PyInt.h"

// namespace Global {

// parametry do u�ytku wewn�trznego
// double Global::tSinceStart=0;
TGround *Global::pGround = NULL;
// char Global::CreatorName1[30]="2001-2004 Maciej Czapkiewicz <McZapkie>";
// char Global::CreatorName2[30]="2001-2003 Marcin Wo�niak <Marcin_EU>";
// char Global::CreatorName3[20]="2004-2005 Adam Bugiel <ABu>";
// char Global::CreatorName4[30]="2004 Arkadiusz �lusarczyk <Winger>";
// char Global::CreatorName5[30]="2003-2009 �ukasz Kirchner <Nbmx>";
std::string Global::asCurrentSceneryPath = "scenery/";
std::string Global::asCurrentTexturePath = std::string(szTexturePath);
std::string Global::asCurrentDynamicPath = "";
int Global::iSlowMotion =
    0; // info o malym FPS: 0-OK, 1-wy��czy� multisampling, 3-promie� 1.5km, 7-1km
TDynamicObject *Global::changeDynObj = NULL; // info o zmianie pojazdu
bool Global::detonatoryOK; // info o nowych detonatorach
double Global::ABuDebug = 0;
std::string Global::asSky = "1";
double Global::fOpenGL = 0.0; // wersja OpenGL - do sprawdzania obecno�ci rozszerze�
bool Global::bOpenGL_1_5 = false; // czy s� dost�pne funkcje OpenGL 1.5
double Global::fLuminance = 1.0; // jasno�� �wiat�a do automatycznego zapalania
int Global::iReCompile = 0; // zwi�kszany, gdy trzeba od�wie�y� siatki
HWND Global::hWnd = NULL; // uchwyt okna
int Global::iCameraLast = -1;
std::string Global::asRelease = "16.0.1172.481";
std::string Global::asVersion =
    "Compilation 2016-08-24, release " + Global::asRelease + "."; // tutaj, bo wysy�any
int Global::iViewMode = 0; // co aktualnie wida�: 0-kabina, 1-latanie, 2-sprz�gi, 3-dokumenty
int Global::iTextMode = 0; // tryb pracy wy�wietlacza tekstowego
int Global::iScreenMode[12] = {0, 0, 0, 0, 0, 0,
                               0, 0, 0, 0, 0, 0}; // numer ekranu wy�wietlacza tekstowego
double Global::fSunDeclination = 0.0; // deklinacja S�o�ca
double Global::fTimeAngleDeg = 0.0; // godzina w postaci k�ta
float Global::fClockAngleDeg[6]; // k�ty obrotu cylindr�w dla zegara cyfrowego
char *Global::szTexturesTGA[4] = {"tga", "dds", "tex", "bmp"}; // lista tekstur od TGA
char *Global::szTexturesDDS[4] = {"dds", "tga", "tex", "bmp"}; // lista tekstur od DDS
int Global::iKeyLast = 0; // ostatnio naci�ni�ty klawisz w celu logowania
GLuint Global::iTextureId = 0; // ostatnio u�yta tekstura 2D
int Global::iPause = 0x10; // globalna pauza ruchu
bool Global::bActive = true; // czy jest aktywnym oknem
int Global::iErorrCounter = 0; // licznik sprawdza� do �ledzenia b��d�w OpenGL
int Global::iTextures = 0; // licznik u�ytych tekstur
TWorld *Global::pWorld = NULL;
cParser *Global::pParser = NULL;
int Global::iSegmentsRendered = 90; // ilo�� segment�w do regulacji wydajno�ci
TCamera *Global::pCamera = NULL; // parametry kamery
TDynamicObject *Global::pUserDynamic = NULL; // pojazd u�ytkownika, renderowany bez trz�sienia
bool Global::bSmudge = false; // czy wy�wietla� smug�, a pojazd u�ytkownika na ko�cu
std::string Global::asTranscript[5]; // napisy na ekranie (widoczne)
TTranscripts Global::tranTexts; // obiekt obs�uguj�cy stenogramy d�wi�k�w na ekranie

// parametry scenerii
vector3 Global::pCameraPosition;
double Global::pCameraRotation;
double Global::pCameraRotationDeg;
vector3 Global::pFreeCameraInit[10];
vector3 Global::pFreeCameraInitAngle[10];
double Global::fFogStart = 1700;
double Global::fFogEnd = 2000;
float Global::Background[3] = { 0.2, 0.4, 0.33 }; 
GLfloat Global::AtmoColor[] = {0.423f, 0.702f, 1.0f};
GLfloat Global::FogColor[] = {0.6f, 0.7f, 0.8f};
GLfloat Global::ambientDayLight[] = {0.40f, 0.40f, 0.45f, 1.0f}; // robocze
GLfloat Global::diffuseDayLight[] = {0.55f, 0.54f, 0.50f, 1.0f};
GLfloat Global::specularDayLight[] = {0.95f, 0.94f, 0.90f, 1.0f};
GLfloat Global::ambientLight[] = {0.80f, 0.80f, 0.85f, 1.0f}; // sta�e
GLfloat Global::diffuseLight[] = {0.85f, 0.85f, 0.80f, 1.0f};
GLfloat Global::specularLight[] = {0.95f, 0.94f, 0.90f, 1.0f};
GLfloat Global::whiteLight[] = {1.00f, 1.00f, 1.00f, 1.0f};
GLfloat Global::noLight[] = {0.00f, 0.00f, 0.00f, 1.0f};
GLfloat Global::darkLight[] = {0.03f, 0.03f, 0.03f, 1.0f}; //�ladowe
GLfloat Global::lightPos[4];
bool Global::bRollFix = true; // czy wykona� przeliczanie przechy�ki
bool Global::bJoinEvents = false; // czy grupowa� eventy o tych samych nazwach
int Global::iHiddenEvents = 1; // czy ��czy� eventy z torami poprzez nazw� toru

// parametry u�ytkowe (jak komu pasuje)
int Global::Keys[MaxKeys];
int Global::iWindowWidth = 800;
int Global::iWindowHeight = 600;
float Global::fDistanceFactor = 768.0; // baza do przeliczania odleg�o�ci dla LoD
int Global::iFeedbackMode = 1; // tryb pracy informacji zwrotnej
int Global::iFeedbackPort = 0; // dodatkowy adres dla informacji zwrotnych
bool Global::bFreeFly = false;
bool Global::bFullScreen = false;
bool Global::bInactivePause = true; // automatyczna pauza, gdy okno nieaktywne
float Global::fMouseXScale = 1.5;
float Global::fMouseYScale = 0.2;
std::string Global::SceneryFile = "td.scn";
std::string Global::asHumanCtrlVehicle = "EU07-424";
int Global::iMultiplayer = 0; // blokada dzia�ania niekt�rych funkcji na rzecz komunikacji
double Global::fMoveLight = -1; // ruchome �wiat�o
double Global::fLatitudeDeg = 52.0; // szeroko�� geograficzna
float Global::fFriction = 1.0; // mno�nik tarcia - KURS90
double Global::fBrakeStep = 1.0; // krok zmiany hamulca dla klawiszy [Num3] i [Num9]
std::string Global::asLang = "pl"; // domy�lny j�zyk - http://tools.ietf.org/html/bcp47

// parametry wydajno�ciowe (np. regulacja FPS, szybko�� wczytywania)
bool Global::bAdjustScreenFreq = true;
bool Global::bEnableTraction = true;
bool Global::bLoadTraction = true;
bool Global::bLiveTraction = true;
int Global::iDefaultFiltering = 9; // domy�lne rozmywanie tekstur TGA bez alfa
int Global::iBallastFiltering = 9; // domy�lne rozmywanie tekstur podsypki
int Global::iRailProFiltering = 5; // domy�lne rozmywanie tekstur szyn
int Global::iDynamicFiltering = 5; // domy�lne rozmywanie tekstur pojazd�w
bool Global::bUseVBO = false; // czy jest VBO w karcie graficznej (czy u�y�)
GLint Global::iMaxTextureSize = 16384; // maksymalny rozmiar tekstury
bool Global::bSmoothTraction = false; // wyg�adzanie drut�w starym sposobem
char **Global::szDefaultExt = Global::szTexturesDDS; // domy�lnie od DDS
int Global::iMultisampling = 2; // tryb antyaliasingu: 0=brak,1=2px,2=4px,3=8px,4=16px
bool Global::bGlutFont = false; // czy tekst generowany przez GLUT32.DLL
int Global::iConvertModels = 7; // tworzenie plik�w binarnych, +2-optymalizacja transform�w
int Global::iSlowMotionMask = -1; // maska wy��czanych w�a�ciwo�ci dla zwi�kszenia FPS
int Global::iModifyTGA = 7; // czy korygowa� pliki TGA dla szybszego wczytywania
// bool Global::bTerrainCompact=true; //czy zapisa� teren w pliku
TAnimModel *Global::pTerrainCompact = NULL; // do zapisania terenu w pliku
std::string Global::asTerrainModel = ""; // nazwa obiektu terenu do zapisania w pliku
double Global::fFpsAverage = 20.0; // oczekiwana wartos� FPS
double Global::fFpsDeviation = 5.0; // odchylenie standardowe FPS
double Global::fFpsMin = 0.0; // dolna granica FPS, przy kt�rej promie� scenerii b�dzie zmniejszany
double Global::fFpsMax = 0.0; // g�rna granica FPS, przy kt�rej promie� scenerii b�dzie zwi�kszany
double Global::fFpsRadiusMax = 3000.0; // maksymalny promie� renderowania
int Global::iFpsRadiusMax = 225; // maksymalny promie� renderowania
double Global::fRadiusFactor = 1.1; // wsp�czynnik jednorazowej zmiany promienia scenerii
bool Global::bOldSmudge = false; // U�ywanie starej smugi

// parametry testowe (do testowania scenerii i obiekt�w)
bool Global::bWireFrame = false;
bool Global::bSoundEnabled = true;
int Global::iWriteLogEnabled = 3; // maska bitowa: 1-zapis do pliku, 2-okienko, 4-nazwy tor�w
bool Global::bManageNodes = true;
bool Global::bDecompressDDS = false; // czy programowa dekompresja DDS

// parametry do kalibracji
// kolejno wsp�czynniki dla pot�g 0, 1, 2, 3 warto�ci odczytanej z urz�dzenia
double Global::fCalibrateIn[6][6] = {{0, 1, 0, 0, 0, 0},
                                     {0, 1, 0, 0, 0, 0},
                                     {0, 1, 0, 0, 0, 0},
                                     {0, 1, 0, 0, 0, 0},
                                     {0, 1, 0, 0, 0, 0},
                                     {0, 1, 0, 0, 0, 0}};
double Global::fCalibrateOut[7][6] = {{0, 1, 0, 0, 0, 0},
                                      {0, 1, 0, 0, 0, 0},
                                      {0, 1, 0, 0, 0, 0},
                                      {0, 1, 0, 0, 0, 0},
                                      {0, 1, 0, 0, 0, 0},
                                      {0, 1, 0, 0, 0, 0},
                                      {0, 1, 0, 0, 0, 0}};
double Global::fCalibrateOutMax[7] = {0, 0, 0, 0, 0, 0, 0};
int Global::iCalibrateOutDebugInfo = -1;
int Global::iPoKeysPWM[7] = {0, 1, 2, 3, 4, 5, 6};
// parametry przej�ciowe (do usuni�cia)
// bool Global::bTimeChange=false; //Ra: ZiomalCl wy��czy� star� wersj� nocy
// bool Global::bRenderAlpha=true; //Ra: wywali�am t� flag�
bool Global::bnewAirCouplers = true;
bool Global::bDoubleAmbient = false; // podw�jna jasno�� ambient
double Global::fTimeSpeed = 1.0; // przyspieszenie czasu, zmienna do test�w
bool Global::bHideConsole = false; // hunter-271211: ukrywanie konsoli
int Global::iBpp = 32; // chyba ju� nie u�ywa si� kart, na kt�rych 16bpp co� poprawi

//---------------------------------------------------------------------------
//---------------------------------------------------------------------------
//---------------------------------------------------------------------------

std::string Global::GetNextSymbol()
{ // pobranie tokenu z aktualnego parsera

	std::string token;
	if( pParser != nullptr ) {

        pParser->getTokens();
        *pParser >> token;
    };
    return token;
};

void Global::LoadIniFile(std::string asFileName)
{
    for ( int i = 0; i < 10; ++i)
    { // zerowanie pozycji kamer
        pFreeCameraInit[i] = vector3(0, 0, 0); // wsp�rz�dne w scenerii
        pFreeCameraInitAngle[i] = vector3(0, 0, 0); // k�ty obrotu w radianach
    }
	cParser parser( asFileName, cParser::buffer_FILE );
	ConfigParse( parser );
};

void Global::ConfigParse(cParser &Parser) {

	std::string token;
	do {

		token = "";
		Parser.getTokens(); Parser >> token;

		if( token == "sceneryfile" ) {

			Parser.getTokens(); Parser >> Global::SceneryFile;
		}
		else if( token == "humanctrlvehicle" ) {

			Parser.getTokens(); Parser >> Global::asHumanCtrlVehicle;
		}
		else if( token == "width" ) {

			Parser.getTokens( 1, false ); Parser >> Global::iWindowWidth;
		}
		else if( token == "height" ) {

			Parser.getTokens( 1, false ); Parser >> Global::iWindowHeight;
		}
		else if( token == "heightbase" ) {

			Parser.getTokens( 1, false ); Parser >> Global::fDistanceFactor;
		}
		else if( token == "bpp" ) {

			Parser.getTokens(); Parser >> token;
			Global::iBpp = ( token == "32" ? 32 : 16 );
		}
		else if( token == "fullscreen" ) {

			Parser.getTokens(); Parser >> token;
			Global::bFullScreen = ( token == "yes" );
		}
		else if( token == "freefly" ) { // Mczapkie-130302

			Parser.getTokens(); Parser >> token;
			Global::bFreeFly = ( token == "yes" );
			Parser.getTokens( 3, false );
			Parser >>
				Global::pFreeCameraInit[ 0 ].x,
				Global::pFreeCameraInit[ 0 ].y,
				Global::pFreeCameraInit[ 0 ].z;
		}
		else if( token == "wireframe" ) {

			Parser.getTokens(); Parser >> token;
			Global::bWireFrame = ( token == "yes" );
		}
		else if( token == "debugmode" ) { // McZapkie! - DebugModeFlag uzywana w mover.pas,
			// warto tez blokowac cheaty gdy false
			Parser.getTokens(); Parser >> token;
			DebugModeFlag = ( token == "yes" );
		}
		else if( token == "soundenabled" ) { // McZapkie-040302 - blokada dzwieku - przyda
			// sie do debugowania oraz na komp. bez karty
			// dzw.
			Parser.getTokens(); Parser >> token;
			Global::bSoundEnabled = ( token == "yes" );
		}
		// else if (str==AnsiString("renderalpha")) //McZapkie-1312302 - dwuprzebiegowe renderowanie
		// bRenderAlpha=(GetNextSymbol().LowerCase()==AnsiString("yes"));
		else if( token == "physicslog" ) { // McZapkie-030402 - logowanie parametrow
			// fizycznych dla kazdego pojazdu z maszynista
			Parser.getTokens(); Parser >> token;
			WriteLogFlag = ( token == "yes" );
		}
		else if( token == "physicsdeactivation" ) { // McZapkie-291103 - usypianie fizyki

			Parser.getTokens(); Parser >> token;
			PhysicActivationFlag = ( token == "yes" );
		}
		else if( token == "debuglog" ) {
			// McZapkie-300402 - wylaczanie log.txt
			Parser.getTokens(); Parser >> token;
			if( token == "yes" ) { Global::iWriteLogEnabled = 3; }
			else if( token == "no" ) { Global::iWriteLogEnabled = 0; }
			else { Global::iWriteLogEnabled = std::atoi( token.c_str() ); }
		}
		else if( token == "adjustscreenfreq" ) {
			// McZapkie-240403 - czestotliwosc odswiezania ekranu
			Parser.getTokens(); Parser >> token;
			Global::bAdjustScreenFreq = ( token == "yes" );
		}
		else if( token == "mousescale" ) {
			// McZapkie-060503 - czulosc ruchu myszy (krecenia glowa)
			Parser.getTokens( 2, false );
			Parser
				>> Global::fMouseXScale
				>> Global::fMouseYScale;
		}
		else if( token == "enabletraction" ) {
			// Winger 040204 - 'zywe' patyki dostosowujace sie do trakcji; Ra 2014-03: teraz �amanie
			Parser.getTokens(); Parser >> token;
			Global::bEnableTraction = ( token == "yes" );
		}
		else if( token == "loadtraction" ) {
			// Winger 140404 - ladowanie sie trakcji
			Parser.getTokens(); Parser >> token;
			Global::bLoadTraction = ( token == "yes" );
		}
		else if( token == "friction" ) { // mno�nik tarcia - KURS90

			Parser.getTokens( 1, false );
			Parser >> Global::fFriction;
		}
		else if( token == "livetraction" ) {
			// Winger 160404 - zaleznosc napiecia loka od trakcji;
			// Ra 2014-03: teraz pr�d przy braku sieci
			Parser.getTokens(); Parser >> token;
			Global::bLiveTraction = ( token == "yes" );
		}
		else if( token == "skyenabled" ) {
			// youBy - niebo
			Parser.getTokens(); Parser >> token;
			Global::asSky = ( token == "yes" ? "1" : "0" );
		}
		else if( token == "managenodes" ) {

			Parser.getTokens(); Parser >> token;
			Global::bManageNodes = ( token == "yes" );
		}
		else if( token == "decompressdds" ) {

			Parser.getTokens(); Parser >> token;
			Global::bDecompressDDS = ( token == "yes" );
		}
		else if( token == "defaultext" ) {
			// ShaXbee - domyslne rozszerzenie tekstur
			Parser.getTokens(); Parser >> token;
			if( token == "tga" ) {
				// domy�lnie od TGA
				Global::szDefaultExt = Global::szTexturesTGA;
			}
		}
		else if( token == "newaircouplers" ) {

			Parser.getTokens(); Parser >> token;
			Global::bnewAirCouplers = ( token == "yes" );
		}
		else if( token == "defaultfiltering" ) {

			Parser.getTokens( 1, false );
			Parser >> Global::iDefaultFiltering;
		}
		else if( token == "ballastfiltering" ) {

			Parser.getTokens( 1, false );
			Parser >> Global::iBallastFiltering;
		}
		else if( token == "railprofiltering" ) {

			Parser.getTokens( 1, false );
			Parser >> Global::iRailProFiltering;
		}
		else if( token == "dynamicfiltering" ) {

			Parser.getTokens( 1, false );
			Parser >> Global::iDynamicFiltering;
		}
		else if( token == "usevbo" ) {

			Parser.getTokens(); Parser >> token;
			Global::bUseVBO = ( token == "yes" );
		}
		else if( token == "feedbackmode" ) {

			Parser.getTokens( 1, false );
			Parser >> Global::iFeedbackMode;
		}
		else if( token == "feedbackport" ) {

			Parser.getTokens( 1, false );
			Parser >> Global::iFeedbackPort;
		}
		else if( token == "multiplayer" ) {

			Parser.getTokens( 1, false );
			Parser >> Global::iMultiplayer;
		}
		else if( token == "maxtexturesize" ) {
			// wymuszenie przeskalowania tekstur
			Parser.getTokens( 1, false );
			int size; Parser >> size;
			if( size <= 64 ) { Global::iMaxTextureSize = 64; }
			else if( size <= 128 ) { Global::iMaxTextureSize = 128; }
			else if( size <= 256 ) { Global::iMaxTextureSize = 256; }
			else if( size <= 512 ) { Global::iMaxTextureSize = 512; }
			else if( size <= 1024 ) { Global::iMaxTextureSize = 1024; }
			else if( size <= 2048 ) { Global::iMaxTextureSize = 2048; }
			else if( size <= 4096 ) { Global::iMaxTextureSize = 4096; }
			else if( size <= 8192 ) { Global::iMaxTextureSize = 8192; }
			else { Global::iMaxTextureSize = 16384; }
		}
		else if( token == "doubleambient" ) {
			// podw�jna jasno�� ambient
			Parser.getTokens(); Parser >> token;
			Global::bDoubleAmbient = ( token == "yes" );
		}
		else if( token == "movelight" ) {
			// numer dnia w roku albo -1
			Parser.getTokens( 1, false );
			Parser >> Global::fMoveLight;
			if( Global::fMoveLight == 0.f ) { // pobranie daty z systemu
				std::time_t timenow = std::time( 0 );
				std::tm *localtime = std::localtime( &timenow );
				Global::fMoveLight = localtime->tm_yday + 1; // numer bie��cego dnia w roku
			}
			if( Global::fMoveLight > 0.f ) // tu jest nadal zwi�kszone o 1
			{ // obliczenie deklinacji wg:
				// http://naturalfrequency.com/Tregenza_Sharples/Daylight_Algorithms/algorithm_1_11.htm
				// Spencer J W Fourier series representation of the position of the sun Search 2 (5)
				// 172 (1971)
				Global::fMoveLight = M_PI / 182.5 * ( Global::fMoveLight - 1.0 ); // numer dnia w postaci k�ta
				fSunDeclination = 0.006918 - 0.3999120 * std::cos( fMoveLight ) +
					0.0702570 * std::sin( fMoveLight ) - 0.0067580 * std::cos( 2 * fMoveLight ) +
					0.0009070 * std::sin( 2 * fMoveLight ) -
					0.0026970 * std::cos( 3 * fMoveLight ) + 0.0014800 * std::sin( 3 * fMoveLight );
			}
		}
		else if( token == "smoothtraction" ) {
			// podw�jna jasno�� ambient
			Parser.getTokens(); Parser >> token;
			Global::bSmoothTraction = ( token == "yes" );
		}
		else if( token == "timespeed" ) {
			// przyspieszenie czasu, zmienna do test�w
			Parser.getTokens( 1, false );
			Parser >> Global::fTimeSpeed;
		}
		else if( token == "multisampling" ) {
			// tryb antyaliasingu: 0=brak,1=2px,2=4px
			Parser.getTokens( 1, false );
			Parser >> Global::iMultisampling;
		}
		else if( token == "glutfont" ) {
			// tekst generowany przez GLUT
			Parser.getTokens(); Parser >> token;
			Global::bGlutFont = ( token == "yes" );
		}
		else if( token == "latitude" ) {
			// szeroko�� geograficzna
			Parser.getTokens( 1, false );
			Parser >> Global::fLatitudeDeg;
		}
		else if( token == "convertmodels" ) {
			// tworzenie plik�w binarnych
			Parser.getTokens( 1, false );
			Parser >> Global::iConvertModels;
		}
		else if( token == "inactivepause" ) {
			// automatyczna pauza, gdy okno nieaktywne
			Parser.getTokens(); Parser >> token;
			Global::bInactivePause = ( token == "yes" );
		}
		else if( token == "slowmotion" ) {
			// tworzenie plik�w binarnych
			Parser.getTokens( 1, false );
			Parser >> Global::iSlowMotionMask;
		}
		else if( token == "modifytga" ) {
			// czy korygowa� pliki TGA dla szybszego wczytywania
			Parser.getTokens( 1, false );
			Parser >> Global::iModifyTGA;
		}
		else if( token == "hideconsole" ) {
			// hunter-271211: ukrywanie konsoli
			Parser.getTokens(); Parser >> token;
			Global::bHideConsole = ( token == "yes" );
		}
		else if( token == "oldsmudge" ) {

			Parser.getTokens(); Parser >> token;
			Global::bOldSmudge = ( token == "yes" );
		}
		else if( token == "rollfix" ) {
			// Ra: poprawianie przechy�ki, aby wewn�trzna szyna by�a "pozioma"
			Parser.getTokens(); Parser >> token;
			Global::bRollFix = ( token == "yes" );
		}
		else if( token == "fpsaverage" ) {
			// oczekiwana wartos� FPS
			Parser.getTokens( 1, false );
			Parser >> Global::fFpsAverage;
		}
		else if( token == "fpsdeviation" ) {
			// odchylenie standardowe FPS
			Parser.getTokens( 1, false );
			Parser >> Global::fFpsDeviation;
		}
		else if( token == "fpsradiusmax" ) {
			// maksymalny promie� renderowania
			Parser.getTokens( 1, false );
			Parser >> Global::fFpsRadiusMax;
		}
        else if(token == "calibratein"){
			// parametry kalibracji wej��
			Parser.getTokens( 1, false );
			int in; Parser >> in;
			if( ( in < 0 ) || ( in > 5 ) ) {
				in = 5; // na ostatni, bo i tak trzeba pomin�� warto�ci
			}
			Parser.getTokens( 4, false );
			Parser
				>> Global::fCalibrateIn[ in ][ 0 ] // wyraz wolny
				>> Global::fCalibrateIn[ in ][ 1 ] // mno�nik
				>> Global::fCalibrateIn[ in ][ 2 ] // mno�nik dla kwadratu
				>> Global::fCalibrateIn[ in ][ 3 ]; // mno�nik dla sze�cianu
			Global::fCalibrateIn[ in ][ 4 ] = 0.0; // mno�nik 4 pot�gi
			Global::fCalibrateIn[ in ][ 5 ] = 0.0; // mno�nik 5 pot�gi
		}
		else if(token == "calibrate5din"){
			// parametry kalibracji wej��
			Parser.getTokens( 1, false );
			int in; Parser >> in;
			if( ( in < 0 ) || ( in > 5 ) ) {
				in = 5; // na ostatni, bo i tak trzeba pomin�� warto�ci
			}
			Parser.getTokens( 6, false );
			Parser
				>> Global::fCalibrateIn[ in ][ 0 ] // wyraz wolny
				>> Global::fCalibrateIn[ in ][ 1 ] // mno�nik
				>> Global::fCalibrateIn[ in ][ 2 ] // mno�nik dla kwadratu
				>> Global::fCalibrateIn[ in ][ 3 ] // mno�nik dla sze�cianu
				>> Global::fCalibrateIn[ in ][ 4 ] // mno�nik 4 pot�gi
				>> Global::fCalibrateIn[ in ][ 5 ]; // mno�nik 5 pot�gi
		}
        else if(token == "calibrateout"){
			// parametry kalibracji wyj��
			Parser.getTokens( 1, false );
			int out; Parser >> out;
			if( ( out < 0 ) || ( out > 6 ) ) {
				out = 6; // na ostatni, bo i tak trzeba pomin�� warto�ci
			}
			Parser.getTokens( 4, false );
			Parser
				>> Global::fCalibrateOut[ out ][ 0 ] // wyraz wolny
				>> Global::fCalibrateOut[ out ][ 1 ] // mno�nik liniowy
				>> Global::fCalibrateOut[ out ][ 2 ] // mno�nik dla kwadratu
				>> Global::fCalibrateOut[ out ][ 3 ]; // mno�nik dla sze�cianu
			Global::fCalibrateOut[ out ][ 4 ] = 0.0; // mno�nik dla 4 pot�gi
			Global::fCalibrateOut[ out ][ 5 ] = 0.0; // mno�nik dla 5 pot�gi
        }
		else if(token == "calibrate5dout"){
			// parametry kalibracji wyj��
			Parser.getTokens( 1, false );
			int out; Parser >> out;
			if( ( out < 0 ) || ( out > 6 ) ) {
				out = 6; // na ostatni, bo i tak trzeba pomin�� warto�ci
			}
			Parser.getTokens( 6, false );
			Parser
				>> Global::fCalibrateOut[ out ][ 0 ] // wyraz wolny
				>> Global::fCalibrateOut[ out ][ 1 ] // mno�nik liniowy
				>> Global::fCalibrateOut[ out ][ 2 ] // mno�nik dla kwadratu
				>> Global::fCalibrateOut[ out ][ 3 ] // mno�nik dla sze�cianu
				>> Global::fCalibrateOut[ out ][ 4 ] // mno�nik dla 4 pot�gi
				>> Global::fCalibrateOut[ out ][ 5 ]; // mno�nik dla 5 pot�gi
		}
		else if(token == "calibrateoutmaxvalues"){
			// maksymalne warto�ci jakie mo�na wy�wietli� na mierniku
			Parser.getTokens( 7, false );
			Parser
				>> Global::fCalibrateOutMax[ 0 ]
				>> Global::fCalibrateOutMax[ 1 ]
				>> Global::fCalibrateOutMax[ 2 ]
				>> Global::fCalibrateOutMax[ 3 ]
				>> Global::fCalibrateOutMax[ 4 ]
				>> Global::fCalibrateOutMax[ 5 ]
				>> Global::fCalibrateOutMax[ 6 ];
		}
		else if( token == "calibrateoutdebuginfo" ) {
			// wyj�cie z info o przebiegu kalibracji
			Parser.getTokens( 1, false );
			Parser >> Global::iCalibrateOutDebugInfo;
		}
		else if( token == "pwm" ) {
			// zmiana numer�w wyj�� PWM
			Parser.getTokens( 2, false );
			int pwm_out, pwm_no;
			Parser
				>> pwm_out
				>> pwm_no;
			Global::iPoKeysPWM[ pwm_out ] = pwm_no;
		}
		else if( token == "brakestep" ) {
			// krok zmiany hamulca dla klawiszy [Num3] i [Num9]
			Parser.getTokens( 1, false );
			Parser >> Global::fBrakeStep;
		}
		else if( token == "joinduplicatedevents" ) {
			// czy grupowa� eventy o tych samych nazwach
			Parser.getTokens(); Parser >> token;
			Global::bJoinEvents = ( token == "yes" );
		}
		else if( token == "hiddenevents" ) {
			// czy ��czy� eventy z torami poprzez nazw� toru
			Parser.getTokens( 1, false );
			Parser >> Global::iHiddenEvents;
		}
		else if( token == "pause" ) {
			// czy po wczytaniu ma by� pauza?
			Parser.getTokens(); Parser >> token;
			iPause |= ( token == "yes" ? 1 : 0 );
		}
		else if( token == "lang" ) {
			// domy�lny j�zyk - http://tools.ietf.org/html/bcp47
			Parser.getTokens( 1, false );
			Parser >> Global::asLang;
		}
		else if( token == "opengl" ) {
			// deklarowana wersja OpenGL, �eby powstrzyma� b��dy
			Parser.getTokens( 1, false );
			Parser >> Global::fOpenGL;
		}
		else if( token == "pyscreenrendererpriority" ) {
			// priority of python screen renderer
			Parser.getTokens(); Parser >> token;
			TPythonInterpreter::getInstance()->setScreenRendererPriority( token.c_str() );
		}
		else if( token == "background" ) {

			Parser.getTokens( 3, false );
			Parser
				>> Global::Background[ 0 ] // r
				>> Global::Background[ 1 ] // g
				>> Global::Background[ 2 ];// b
		}
	}
	while( ( token != "" )
		&& ( token != "endconfig" ) ); //(!Parser->EndOfFile)
    // na koniec troch� zale�no�ci
    if (!bLoadTraction) // wczytywanie drut�w i s�up�w
    { // tutaj wy��czenie, bo mog� nie by� zdefiniowane w INI
        bEnableTraction = false; // false = pantograf si� nie po�amie
        bLiveTraction = false; // false = pantografy zawsze zbieraj� 95% MaxVoltage
    }
    // if (fMoveLight>0) bDoubleAmbient=false; //wtedy tylko jedno �wiat�o ruchome
    // if (fOpenGL<1.3) iMultisampling=0; //mo�na by z g�ry wy��czy�, ale nie mamy jeszcze fOpenGL
    if (iMultisampling)
    { // antyaliasing ca�oekranowy wy��cza rozmywanie drut�w
        bSmoothTraction = false;
    }
    if (iMultiplayer > 0)
    {
        bInactivePause = false; // okno "w tle" nie mo�e pauzowa�, je�li w��czona komunikacja
        // pauzowanie jest zablokowane dla (iMultiplayer&2)>0, wi�c iMultiplayer=1 da si� zapauzowa�
        // (tryb instruktora)
    }
    fFpsMin = fFpsAverage -
              fFpsDeviation; // dolna granica FPS, przy kt�rej promie� scenerii b�dzie zmniejszany
    fFpsMax = fFpsAverage +
              fFpsDeviation; // g�rna granica FPS, przy kt�rej promie� scenerii b�dzie zwi�kszany
    if (iPause)
        iTextMode = VK_F1; // jak pauza, to pokaza� zegar
/*  this won't execute anymore with the old parser removed
	// TBD: remove, or launch depending on passed flag?
    if (qp)
    { // to poni�ej wykonywane tylko raz, jedynie po wczytaniu eu07.ini
        Console::ModeSet(iFeedbackMode, iFeedbackPort); // tryb pracy konsoli sterowniczej
        iFpsRadiusMax = 0.000025 * fFpsRadiusMax *
                        fFpsRadiusMax; // maksymalny promie� renderowania 3000.0 -> 225
        if (iFpsRadiusMax > 400)
            iFpsRadiusMax = 400;
        if (fDistanceFactor > 1.0)
        { // dla 1.0 specjalny tryb bez przeliczania
            fDistanceFactor =
                iWindowHeight /
                fDistanceFactor; // fDistanceFactor>1.0 dla rozdzielczo�ci wi�kszych ni� bazowa
            fDistanceFactor *=
                (iMultisampling + 1.0) *
                fDistanceFactor; // do kwadratu, bo wi�kszo�� odleg�o�ci to ich kwadraty
        }
    }
*/
}

void Global::InitKeys(std::string asFileName)
{
    //    if (FileExists(asFileName))
    //    {
    //       Error("Chwilowo plik keys.ini nie jest obs�ugiwany. �aduj� standardowe
    //       ustawienia.\nKeys.ini file is temporarily not functional, loading default keymap...");
    /*        TQueryParserComp *Parser;
            Parser=new TQueryParserComp(NULL);
            Parser->LoadStringToParse(asFileName);

            for (int keycount=0; keycount<MaxKeys; keycount++)
             {
              Keys[keycount]=Parser->GetNextSymbol().ToInt();
             }

            delete Parser;
    */
    //    }
    //    else
    {
        Keys[k_IncMainCtrl] = VK_ADD;
        Keys[k_IncMainCtrlFAST] = VK_ADD;
        Keys[k_DecMainCtrl] = VK_SUBTRACT;
        Keys[k_DecMainCtrlFAST] = VK_SUBTRACT;
        Keys[k_IncScndCtrl] = VK_DIVIDE;
        Keys[k_IncScndCtrlFAST] = VK_DIVIDE;
        Keys[k_DecScndCtrl] = VK_MULTIPLY;
        Keys[k_DecScndCtrlFAST] = VK_MULTIPLY;
        ///*NORMALNE
        Keys[k_IncLocalBrakeLevel] = VK_NUMPAD1; // VK_NUMPAD7;
        // Keys[k_IncLocalBrakeLevelFAST]=VK_END;  //VK_HOME;
        Keys[k_DecLocalBrakeLevel] = VK_NUMPAD7; // VK_NUMPAD1;
        // Keys[k_DecLocalBrakeLevelFAST]=VK_HOME; //VK_END;
        Keys[k_IncBrakeLevel] = VK_NUMPAD3; // VK_NUMPAD9;
        Keys[k_DecBrakeLevel] = VK_NUMPAD9; // VK_NUMPAD3;
        Keys[k_Releaser] = VK_NUMPAD6;
        Keys[k_EmergencyBrake] = VK_NUMPAD0;
        Keys[k_Brake3] = VK_NUMPAD8;
        Keys[k_Brake2] = VK_NUMPAD5;
        Keys[k_Brake1] = VK_NUMPAD2;
        Keys[k_Brake0] = VK_NUMPAD4;
        Keys[k_WaveBrake] = VK_DECIMAL;
        //*/
        /*MOJE
                Keys[k_IncLocalBrakeLevel]=VK_NUMPAD3;  //VK_NUMPAD7;
                Keys[k_IncLocalBrakeLevelFAST]=VK_NUMPAD3;  //VK_HOME;
                Keys[k_DecLocalBrakeLevel]=VK_DECIMAL;  //VK_NUMPAD1;
                Keys[k_DecLocalBrakeLevelFAST]=VK_DECIMAL; //VK_END;
                Keys[k_IncBrakeLevel]=VK_NUMPAD6;  //VK_NUMPAD9;
                Keys[k_DecBrakeLevel]=VK_NUMPAD9;   //VK_NUMPAD3;
                Keys[k_Releaser]=VK_NUMPAD5;
                Keys[k_EmergencyBrake]=VK_NUMPAD0;
                Keys[k_Brake3]=VK_NUMPAD2;
                Keys[k_Brake2]=VK_NUMPAD1;
                Keys[k_Brake1]=VK_NUMPAD4;
                Keys[k_Brake0]=VK_NUMPAD7;
                Keys[k_WaveBrake]=VK_NUMPAD8;
        */
        Keys[k_AntiSlipping] = VK_RETURN;
        Keys[k_Sand] = VkKeyScan('s');
        Keys[k_Main] = VkKeyScan('m');
        Keys[k_Active] = VkKeyScan('w');
        Keys[k_Battery] = VkKeyScan('j');
        Keys[k_DirectionForward] = VkKeyScan('d');
        Keys[k_DirectionBackward] = VkKeyScan('r');
        Keys[k_Fuse] = VkKeyScan('n');
        Keys[k_Compressor] = VkKeyScan('c');
        Keys[k_Converter] = VkKeyScan('x');
        Keys[k_MaxCurrent] = VkKeyScan('f');
        Keys[k_CurrentAutoRelay] = VkKeyScan('g');
        Keys[k_BrakeProfile] = VkKeyScan('b');
        Keys[k_CurrentNext] = VkKeyScan('z');

        Keys[k_Czuwak] = VkKeyScan(' ');
        Keys[k_Horn] = VkKeyScan('a');
        Keys[k_Horn2] = VkKeyScan('a');

        Keys[k_FailedEngineCutOff] = VkKeyScan('e');

        Keys[k_MechUp] = VK_PRIOR;
        Keys[k_MechDown] = VK_NEXT;
        Keys[k_MechLeft] = VK_LEFT;
        Keys[k_MechRight] = VK_RIGHT;
        Keys[k_MechForward] = VK_UP;
        Keys[k_MechBackward] = VK_DOWN;

        Keys[k_CabForward] = VK_HOME;
        Keys[k_CabBackward] = VK_END;

        Keys[k_Couple] = VK_INSERT;
        Keys[k_DeCouple] = VK_DELETE;

        Keys[k_ProgramQuit] = VK_F10;
        // Keys[k_ProgramPause]=VK_F3;
        Keys[k_ProgramHelp] = VK_F1;
        // Keys[k_FreeFlyMode]=VK_F4;
        Keys[k_WalkMode] = VK_F5;

        Keys[k_OpenLeft] = VkKeyScan(',');
        Keys[k_OpenRight] = VkKeyScan('.');
        Keys[k_CloseLeft] = VkKeyScan(',');
        Keys[k_CloseRight] = VkKeyScan('.');
        Keys[k_DepartureSignal] = VkKeyScan('/');

        // Winger 160204 - obsluga pantografow
        Keys[k_PantFrontUp] = VkKeyScan('p'); // Ra: zamieniony przedni z tylnym
        Keys[k_PantFrontDown] = VkKeyScan('p');
        Keys[k_PantRearUp] = VkKeyScan('o');
        Keys[k_PantRearDown] = VkKeyScan('o');
        // Winger 020304 - ogrzewanie
        Keys[k_Heating] = VkKeyScan('h');
        Keys[k_LeftSign] = VkKeyScan('y');
        Keys[k_UpperSign] = VkKeyScan('u');
        Keys[k_RightSign] = VkKeyScan('i');
        Keys[k_EndSign] = VkKeyScan('t');

        Keys[k_SmallCompressor] = VkKeyScan('v');
        Keys[k_StLinOff] = VkKeyScan('l');
        // ABu 090305 - przyciski uniwersalne, do roznych bajerow :)
        Keys[k_Univ1] = VkKeyScan('[');
        Keys[k_Univ2] = VkKeyScan(']');
        Keys[k_Univ3] = VkKeyScan(';');
        Keys[k_Univ4] = VkKeyScan('\'');
    }
}
/*
vector3 Global::GetCameraPosition()
{
    return pCameraPosition;
}
*/
void Global::SetCameraPosition(vector3 pNewCameraPosition)
{
    pCameraPosition = pNewCameraPosition;
}

void Global::SetCameraRotation(double Yaw)
{ // ustawienie bezwzgl�dnego kierunku kamery z korekcj� do przedzia�u <-M_PI,M_PI>
    pCameraRotation = Yaw;
    while (pCameraRotation < -M_PI)
        pCameraRotation += 2 * M_PI;
    while (pCameraRotation > M_PI)
        pCameraRotation -= 2 * M_PI;
    pCameraRotationDeg = pCameraRotation * 180.0 / M_PI;
}

void Global::BindTexture(GLuint t)
{ // ustawienie aktualnej tekstury, tylko gdy si� zmienia
    if (t != iTextureId)
    {
        iTextureId = t;
    }
};

void Global::TrainDelete(TDynamicObject *d)
{ // usuni�cie pojazdu prowadzonego przez u�ytkownika
    if (pWorld)
        pWorld->TrainDelete(d);
};

TDynamicObject * Global::DynamicNearest()
{ // ustalenie pojazdu najbli�szego kamerze
    return pGround->DynamicNearest(pCamera->Pos);
};

TDynamicObject * Global::CouplerNearest()
{ // ustalenie pojazdu najbli�szego kamerze
    return pGround->CouplerNearest(pCamera->Pos);
};

bool Global::AddToQuery(TEvent *event, TDynamicObject *who)
{
    return pGround->AddToQuery(event, who);
};
//---------------------------------------------------------------------------

bool Global::DoEvents()
{ // wywo�ywa� czasem, �eby nie robi� wra�enia zawieszonego
    MSG msg;
    while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
    {
        if (msg.message == WM_QUIT)
            return FALSE;
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return TRUE;
}
//---------------------------------------------------------------------------

TTranscripts::TTranscripts()
{
    iCount = 0; // brak linijek do wy�wietlenia
    iStart = 0; // wype�nia� od linijki 0
    for (int i = 0; i < MAX_TRANSCRIPTS; ++i)
    { // to do konstruktora mo�na by da�
        aLines[i].fHide = -1.0; // wolna pozycja (czas symulacji, 360.0 to doba)
        aLines[i].iNext = -1; // nie ma kolejnej
    }
    fRefreshTime = 360.0; // warto�c zaporowa
};
TTranscripts::~TTranscripts(){};
void TTranscripts::AddLine(char const *txt, float show, float hide, bool it)
{ // dodanie linii do tabeli, (show) i (hide) w [s] od aktualnego czasu
    if (show == hide)
        return; // komentarz jest ignorowany
    show = Global::fTimeAngleDeg + show / 240.0; // je�li doba to 360, to 1s b�dzie r�wne 1/240
    hide = Global::fTimeAngleDeg + hide / 240.0;
    int i = iStart, j, k; // od czego� trzeba zacz��
    while ((aLines[i].iNext >= 0) ? (aLines[aLines[i].iNext].fShow <= show) :
                                    false) // p�ki nie koniec i wcze�niej puszczane
        i = aLines[i].iNext; // przej�cie do kolejnej linijki
    //(i) wskazuje na lini�, po kt�rej nale�y wstawi� dany tekst, chyba �e
    while (txt ? *txt : false)
        for (j = 0; j < MAX_TRANSCRIPTS; ++j)
            if (aLines[j].fHide < 0.0)
            { // znaleziony pierwszy wolny
                aLines[j].iNext = aLines[i].iNext; // dotychczasowy nast�pny b�dzie za nowym
                if (aLines[iStart].fHide < 0.0) // je�li tablica jest pusta
                    iStart = j; // fHide trzeba sprawdzi� przed ewentualnym nadpisaniem, gdy i=j=0
                else
                    aLines[i].iNext = j; // a nowy b�dzie za tamtym wcze�niejszym
                aLines[j].fShow = show; // wy�wietla� od
                aLines[j].fHide = hide; // wy�wietla� do
                aLines[j].bItalic = it;
                aLines[j].asText = std::string(txt); // bez sensu, wystarczy�by wska�nik
                if ((k = aLines[j].asText.find("|")) != std::string::npos)
                { // jak jest podzia� linijki na wiersze
                    aLines[j].asText = aLines[j].asText.substr(0, k - 1);
                    txt += k;
                    i = j; // kolejna linijka dopisywana b�dzie na koniec w�a�nie dodanej
                }
                else
                    txt = NULL; // koniec dodawania
                if (fRefreshTime > show) // je�li od�wie�acz ustawiony jest na p�niej
                    fRefreshTime = show; // to od�wie�y� wcze�niej
                break; // wi�cej ju� nic
            }
};
void TTranscripts::Add(char const *txt, float len, bool backgorund)
{ // dodanie tekst�w, d�ugo�� d�wi�ku, czy istotne
    if (!txt)
        return; // pusty tekst
    int i = 0, j = int(0.5 + 10.0 * len); //[0.1s]
    if (*txt == '[')
    { // powinny by� dwa nawiasy
        while (*++txt ? *txt != ']' : false)
            if ((*txt >= '0') && (*txt <= '9'))
                i = 10 * i + int(*txt - '0'); // pierwsza liczba a� do ]
        if (*txt ? *++txt == '[' : false)
        {
            j = 0; // drugi nawias okre�la czas zako�czenia wy�wietlania
            while (*++txt ? *txt != ']' : false)
                if ((*txt >= '0') && (*txt <= '9'))
                    j = 10 * j + int(*txt - '0'); // druga liczba a� do ]
            if (*txt)
                ++txt; // pomini�cie drugiego ]
        }
    }
    AddLine(txt, 0.1 * i, 0.1 * j, false);
};
void TTranscripts::Update()
{ // usuwanie niepotrzebnych (nie cz�ciej ni� 10 razy na sekund�)
    if (fRefreshTime > Global::fTimeAngleDeg)
        return; // nie czas jeszcze na zmiany
    // czas od�wie�enia mo�na ustali� wg tabelki, kiedy co� si� w niej zmienia
    fRefreshTime = Global::fTimeAngleDeg + 360.0; // warto�� zaporowa
    int i = iStart, j = -1; // od czego� trzeba zacz��
    bool change = false; // czy zmienia� napisy?
    do
    {
        if (aLines[i].fHide >= 0.0) // o ile aktywne
            if (aLines[i].fHide < Global::fTimeAngleDeg)
            { // gdy czas wy�wietlania up�yn��
                aLines[i].fHide = -1.0; // teraz b�dzie woln� pozycj�
                if (i == iStart)
                    iStart = aLines[i].iNext >= 0 ? aLines[i].iNext : 0; // przestawienie pierwszego
                else if (j >= 0)
                    aLines[j].iNext = aLines[i].iNext; // usuni�cie ze �rodka
                change = true;
            }
            else
            { // gdy ma by� pokazane
                if (aLines[i].fShow > Global::fTimeAngleDeg) // b�dzie pokazane w przysz�o�ci
                    if (fRefreshTime > aLines[i].fShow) // a nie ma nic wcze�niej
                        fRefreshTime = aLines[i].fShow;
                if (fRefreshTime > aLines[i].fHide)
                    fRefreshTime = aLines[i].fHide;
            }
        // mo�na by jeszcze wykrywa�, kt�re nowe maj� by� pokazane
        j = i;
        i = aLines[i].iNext; // kolejna linijka
    } while (i >= 0); // p�ki po tablicy
    change = true; // bo na razie nie ma warunku, �e co� si� doda�o
    if (change)
    { // aktualizacja linijek ekranowych
        i = iStart;
        j = -1;
        do
        {
            if (aLines[i].fHide > 0.0) // je�li nie ukryte
                if (aLines[i].fShow < Global::fTimeAngleDeg) // to dodanie linijki do wy�wietlania
                    if (j < 5 - 1) // ograniczona liczba linijek
                        Global::asTranscript[++j] = aLines[i].asText; // skopiowanie tekstu
            i = aLines[i].iNext; // kolejna linijka
        } while (i >= 0); // p�ki po tablicy
        for (++j; j < 5; ++j)
            Global::asTranscript[j] = ""; // i czyszczenie nieu�ywanych linijek
    }
};

// Ra: tymczasowe rozwi�zanie kwestii zagranicznych (czeskich) napis�w
char bezogonkowo[] = "E?,?\"_++?%S<STZZ?`'\"\".--??s>stzz"
                     "�^^L$A|S^CS<--RZo�,l'uP.,as>L\"lz"
                     "RAAAALCCCEEEEIIDDNNOOOOxRUUUUYTB"
                     "raaaalccceeeeiiddnnoooo-ruuuuyt?";

std::string Global::Bezogonkow(std::string str, bool _)
{ // wyci�cie liter z ogonkami, bo OpenGL nie umie wy�wietli�
    for (unsigned int i = 1; i <= str.length(); ++i)
        if (str[i] & 0x80)
            str[i] = bezogonkowo[str[i] & 0x7F];
        else if (str[i] < ' ') // znaki steruj�ce nie s� obs�ugiwane
            str[i] = ' ';
        else if (_)
            if (str[i] == '_') // nazwy stacji nie mog� zawiera� spacji
                str[i] = ' '; // wi�c trzeba wy�wietla� inaczej
    return str;
};

double Global::Min0RSpeed(double vel1, double vel2)
{ // rozszerzenie funkcji Min0R o warto�ci -1.0
	if( vel1 == -1.0 ) { vel1 = std::numeric_limits<double>::max(); }
	if( vel2 == -1.0 ) { vel2 = std::numeric_limits<double>::max();	}
	return Min0R(vel1, vel2);
};

double Global::CutValueToRange(double min, double value, double max)
{ // przycinanie wartosci do podanych granic
	value = Max0R(value, min);
	value = Min0R(value, max);
	return value;
};
