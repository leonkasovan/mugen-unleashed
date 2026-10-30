/**********************************************************************

  resamplesubs.c

  Real-time library interface by Dominic Mazzoni

  Based on resample-1.7:
    http://www-ccrma.stanford.edu/~jos/resample/

  Dual-licensed as LGPL and BSD; see README.md and LICENSE* files.

  This file provides the routines that do sample-rate conversion
  on small arrays, calling routines from filterkit.

**********************************************************************/

/* Definitions */
#include "resample_defs.h"

#include "filterkit.h"

#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <string.h>

/* Sampling rate up-conversion only subroutine;
 * Slightly faster than down-conversion;
 */
int lrsSrcUp(float X[],
             float Y[],
             double factor,
             double *TimePtr,
             UWORD Nx,
             UWORD Nwing,
             float LpScl,
             float Imp[],
             float ImpD[],
             BOOL Interp,
             int Channels)
{
    float *Xp, *Ystart;
    double CurrentTime = *TimePtr;
    double dt;                 /* Step through input signal */ 
    double endTime;            /* When Time reaches EndTime, return to user */
    int ch;

    dt = 1.0/factor;           /* Output sampling period */
    
    Ystart = Y;
    endTime = CurrentTime + Nx;
    while (CurrentTime < endTime)
    {
        double LeftPhase = CurrentTime-floor(CurrentTime);
        double RightPhase = 1.0 - LeftPhase;

        /* X and Y hold interleaved frames, so a step of one frame is
           Channels floats. CurrentTime counts frames, like Nx. */
        for (ch = 0; ch < Channels; ch++)
        {
            float v;

            Xp = &X[((int)CurrentTime) * Channels + ch];
            /* Perform left-wing inner product */
            v = lrsFilterUp(Imp, ImpD, Nwing, Interp, Xp,
                            LeftPhase, -Channels);
            /* Perform right-wing inner product */
            v += lrsFilterUp(Imp, ImpD, Nwing, Interp, Xp + Channels,
                             RightPhase, Channels);

            v *= LpScl;   /* Normalize for unity filter gain */

            Y[ch] = v;               /* Deposit output */
        }
        Y += Channels;

        CurrentTime += dt;      /* Move to next sample by time increment */
    }

    *TimePtr = CurrentTime;
    return (int)((Y - Ystart) / Channels);   /* Return the number of output frames */
}

/* Sampling rate conversion subroutine */

int lrsSrcUD(float X[],
             float Y[],
             double factor,
             double *TimePtr,
             UWORD Nx,
             UWORD Nwing,
             float LpScl,
             float Imp[],
             float ImpD[],
             BOOL Interp,
             int Channels)
{
    float *Xp, *Ystart;
    int ch;

    double CurrentTime = (*TimePtr);
    double dh;                 /* Step through filter impulse response */
    double dt;                 /* Step through input signal */
    double endTime;            /* When Time reaches EndTime, return to user */
    
    dt = 1.0/factor;            /* Output sampling period */
    
    dh = MIN(Npc, factor*Npc);  /* Filter sampling period */
    
    Ystart = Y;
    endTime = CurrentTime + Nx;
    while (CurrentTime < endTime)
    {
        double LeftPhase = CurrentTime-floor(CurrentTime);
        double RightPhase = 1.0 - LeftPhase;

        for (ch = 0; ch < Channels; ch++)
        {
            float v;

            Xp = &X[((int)CurrentTime) * Channels + ch];
            /* Perform left-wing inner product */
            v = lrsFilterUD(Imp, ImpD, Nwing, Interp, Xp,
                            LeftPhase, -Channels, dh);
            /* Perform right-wing inner product */
            v += lrsFilterUD(Imp, ImpD, Nwing, Interp, Xp + Channels,
                             RightPhase, Channels, dh);

            v *= LpScl;   /* Normalize for unity filter gain */
            Y[ch] = v;               /* Deposit output */
        }
        Y += Channels;

        CurrentTime += dt;      /* Move to next sample by time increment */
    }

    *TimePtr = CurrentTime;
    return (int)((Y - Ystart) / Channels);   /* Return the number of output frames */
}
