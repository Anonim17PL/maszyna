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
    Copyright (C) 2001-2004  Marcin Wozniak and others

*/

#include "stdafx.h"
#include "EvLaunch.h"
#include "Globals.h"
#include "Logs.h"
#include "Usefull.h"
#include "McZapkie/mctools.h"
#include "Event.h"
#include "MemCell.h"
#include "mtable.h"
#include "Timer.h"
#include "parser.h"
#include "Console.h"

using namespace Mtable;

//---------------------------------------------------------------------------

TEventLauncher::TEventLauncher()
{ // ustawienie pocz�tkowych warto�ci dla wszystkich zmiennych
    iKey = 0;
    DeltaTime = -1;
    UpdatedTime = 0;
    fVal1 = fVal2 = 0;
    szText = NULL;
    iHour = iMinute = -1; // takiego czasu nigdy nie b�dzie
    dRadius = 0;
    Event1 = Event2 = NULL;
    MemCell = NULL;
    iCheckMask = 0;
}

TEventLauncher::~TEventLauncher()
{
    SafeDeleteArray(szText);
}

void TEventLauncher::Init()
{
}

bool TEventLauncher::Load(cParser *parser)
{ // wczytanie wyzwalacza zdarze�
    std::string token;
    parser->getTokens();
    *parser >> dRadius; // promie� dzia�ania
    if (dRadius > 0.0)
        dRadius *= dRadius; // do kwadratu, pod warunkiem, �e nie jest ujemne
    parser->getTokens(); // klawisz steruj�cy
    *parser >> token;
    if (token != "none")
    {
        if (token.length() == 1)
            iKey = VkKeyScan(token[1]); // jeden znak jest konwertowany na kod klawisza
        else
            iKey = stol_def(token,0); // a jak wi�cej, to jakby numer klawisza jest
    }
    parser->getTokens();
    *parser >> DeltaTime;
    if (DeltaTime < 0)
        DeltaTime = -DeltaTime; // dla ujemnego zmieniamy na dodatni
    else if (DeltaTime > 0)
    { // warto�� dodatnia oznacza wyzwalanie o okre�lonej godzinie
        iMinute = int(DeltaTime) % 100; // minuty s� najm�odszymi cyframi dziesietnymi
        iHour = int(DeltaTime - iMinute) / 100; // godzina to setki
        DeltaTime = 0; // bez powt�rze�
        WriteLog("EventLauncher at " + std::to_string(iHour) + ":" +
                 std::to_string(iMinute)); // wy�wietlenie czasu
    }
    parser->getTokens();
    *parser >> token;
    asEvent1Name = token; // pierwszy event
    parser->getTokens();
    *parser >> token;
    asEvent2Name = token; // drugi event
    if ((asEvent2Name == "end") || (asEvent2Name == "condition"))
    { // drugiego eventu mo�e nie by�, bo s� z tym problemy, ale ciii...
		token = asEvent2Name; // rozpoznane s�owo idzie do dalszego przetwarzania
        asEvent2Name = "none"; // a drugiego eventu nie ma
    }
    else
    { // gdy s� dwa eventy
        parser->getTokens();
        *parser >> token;
        //str = AnsiString(token.c_str());
    }
    if (token == "condition")
    { // obs�uga wyzwalania warunkowego
        parser->getTokens();
        *parser >> token;
		asMemCellName = token;
        parser->getTokens();
        *parser >> token;
        SafeDeleteArray(szText);
        szText = new char[256];
        strcpy(szText, token.c_str());
        if (token.compare("*") != 0) //*=nie bra� command pod uwag�
            iCheckMask |= conditional_memstring;
        parser->getTokens();
        *parser >> token;
        if (token.compare("*") != 0) //*=nie bra� warto�ci 1. pod uwag�
        {
            iCheckMask |= conditional_memval1;
            //str = AnsiString(token.c_str());
            fVal1 = atof(token.c_str());
        }
        else
            fVal1 = 0;
        parser->getTokens();
        *parser >> token;
        if (token.compare("*") != 0) //*=nie bra� warto�ci 2. pod uwag�
        {
            iCheckMask |= conditional_memval2;
            //str = AnsiString(token.c_str());
            fVal2 = atof(token.c_str());
        }
        else
            fVal2 = 0;
        parser->getTokens(); // s�owo zamykaj�ce
        *parser >> token;
    }
    return true;
};

bool TEventLauncher::Render()
{ //"renderowanie" wyzwalacza
    bool bCond = false;
    if (iKey != 0)
    {
        if (Global::bActive) // tylko je�li okno jest aktywne
            bCond = (Console::Pressed(iKey)); // czy klawisz wci�ni�ty
    }
    if (DeltaTime > 0)
    {
        if (UpdatedTime > DeltaTime)
        {
            UpdatedTime = 0; // naliczanie od nowa
            bCond = true;
        }
        else
            UpdatedTime += Timer::GetDeltaTime(); // aktualizacja naliczania czasu
    }
    else
    { // je�li nie cykliczny, to sprawdzi� czas
        if (GlobalTime->hh == iHour)
        {
            if (GlobalTime->mm == iMinute)
            { // zgodno�� czasu uruchomienia
                if (UpdatedTime < 10)
                {
                    UpdatedTime = 20; // czas do kolejnego wyzwolenia?
                    bCond = true;
                }
            }
        }
        else
            UpdatedTime = 1;
    }
    if (bCond) // je�li spe�niony zosta� warunek
    {
        if ((iCheckMask != 0) && MemCell) // sprawdzanie warunku na kom�rce pami�ci
            bCond = MemCell->Compare(szText, fVal1, fVal2, iCheckMask);
    }
    return bCond; // sprawdzanie dRadius w Ground.cpp
}

bool TEventLauncher::IsGlobal()
{ // sprawdzenie, czy jest globalnym wyzwalaczem czasu
    if (DeltaTime == 0)
        if (iHour >= 0)
            if (iMinute >= 0)
                if (dRadius < 0.0) // bez ograniczenia zasi�gu
                    return true;
    return false;
};
//---------------------------------------------------------------------------
