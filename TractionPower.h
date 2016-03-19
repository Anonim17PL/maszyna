/*
This Source Code Form is subject to the
terms of the Mozilla Public License, v.
2.0. If a copy of the MPL was not
distributed with this file, You can
obtain one at
http://mozilla.org/MPL/2.0/.
*/

#ifndef TractionPowerH
#define TractionPowerH
#include "parser.h" //Tolaris-010603

class TGroundNode;

class TTractionPowerSource
{
  private:
    double NominalVoltage;
    double VoltageFrequency;
    double InternalRes;
    double MaxOutputCurrent;
    double FastFuseTimeOut;
    int FastFuseRepetition;
    double SlowFuseTimeOut;
    bool Recuperation;

    double TotalCurrent;
    double TotalAdmitance;
    double TotalPreviousAdmitance;
    double OutputVoltage;
    bool FastFuse;
    bool SlowFuse;
    double FuseTimer;
    int FuseCounter;
    TGroundNode *gMyNode; // wska�nik na w�ze� rodzica

  protected:
  public: // zmienne publiczne
    TTractionPowerSource *psNode[2]; // zasilanie na ko�cach dla sekcji
    bool bSection; // czy jest sekcj�
  public:
    // AnsiString asName;
    TTractionPowerSource(TGroundNode *node);
    ~TTractionPowerSource();
    void Init(double u, double i);
    bool Load(cParser *parser);
    bool Render();
    bool Update(double dt);
    double CurrentGet(double res);
    void VoltageSet(double v)
    {
        NominalVoltage = v;
    };
    void PowerSet(TTractionPowerSource *ps);
};

//---------------------------------------------------------------------------
#endif
