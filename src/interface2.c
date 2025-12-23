/* GTK4 port of src/interface2.c (originally GTK2)
 *
 * Notes:
 * - Replaces GtkTable with GtkGrid, uses GtkScale, GtkComboBoxText, GtkSeparator.
 * - Replaces manual main loop with GtkApplication (activate callback builds UI).
 * - Uses gtk_window_set_child and gtk_widget_set_visible where needed.
 *
 * Build: link with pkg-config gtk4:
 *   gcc ... $(pkg-config --cflags --libs gtk4)
 *
 */

#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include <gtk/gtk.h>

#include "mx44.h"

/* hint_t groups */
#define MISC 0
#define ENV1 1 /* time  */
#define ENV2 2 /* level */
#define ENV3 3 /* other */
#define MOD1 4 /* phase */
#define MOD2 5 /* amplitude */
#define FREQ 6
#define BIAS 7

#define DELAY 8
#define DIST 9
#define LFO 10

#define CSZ 15 // cell size

extern Mx44state *      mx44;

static Mx44patch *      mx44patch;      // Stored patches
static char*            mx44patchNo;    // index of patch in use
static Mx44patch*       mx44tmpPatch;   // copy of patch in use.

static int midichannel = 0;
static int group,bank,patch;
static int patch_change =1;

static int saving;
static int copied = 0;

static const char *op_label[]= {
        "Op 1",
        "Op 2",
        "Op 3",
        "Op 4",
        NULL
};

static const char *od_tips []= {
        "1st",
        "2nd",
        "3rd",
        "4th",
        "5th",
        "6th",
        "7th",
        "8th",
        NULL
};

static const char *temp_tips []= {
        " Even Tempered (Hammond Tonewheel) ",
        " Well Tempered (Werckmeister IV) ",
        " Mean Tone (Italian Renaisance Cembalo) ",
        " Natural Open D (Modern Indian Shruti) "
};

static const char *shruti_tips [] = {
     /* "'D' tonica (Sa)",  */
        " pythagorean limma | minor diatonic semitone (ri)",
        " minor- | major whole tone (RI)",
        " pythagorean- | just minor third (ga)",
        " just- | pythagorean major third (GA)",
        " perfect- | acute fourth (MA)",
        " just- | pythagorean tritonus (ma)",
     /* "'A' perfect fifth (Pa)",   */
        " pythagorean- | just minor sixth (da)",
        " just- | pythagorean major sixth (DA)",
        " pythagorean- | just minor seventh (ni)",
        " just- | pythagorean major seventh (NI)"
};


static GtkWidget        *window1 = NULL;
static GtkWidget        *scale_table;
static GtkWidget        *patchname = NULL;
static GtkWidget        *patch_group_1 = NULL;
static GtkWidget        *patch_group_2 = NULL;
static GtkComboBoxText  *bank_entry = NULL;
static GtkComboBoxText  *patch_entry = NULL;
static GtkAdjustment    *midichannel_spinner_adj = NULL;
static GtkAdjustment    *spin_adj = NULL;
static GtkToggleButton  *savebutton = NULL;

volatile struct
{
  Mx44patch *tmp;
  int channel ;
  int number;
} newpatch = { 0, 0, 0 };


typedef struct _hint
{
  int op;
  int group;
  int index;
  float min;
  float max;
  char * spinlabel;

}hint_t;

static struct mx44patch_edit
{
  GtkWidget *pm[4][4]; // [destination] [source]
  GtkWidget *am[4][4];
  GtkWidget *mix[4][2]; // [op] [level/balance]

  GtkWidget *env_level[4][8]; // [op] [stage]
  GtkWidget *env_time[4][8];

  GtkWidget *wheelbutton[4][2];

  GtkWidget *copybutton[4];
  GtkWidget *pastebutton[4];

  GtkWidget *od[4][8];
  GtkWidget *od_group[4];
  GtkWidget *wawebutton[4];
  GtkWidget *lowpassbutton[4];
  GtkWidget *waweshapebutton[4];
  GtkWidget *phasefollowkeybutton[4];
  GtkWidget *magicbutton[4];
  GtkWidget *expressionbutton[4];

  GtkWidget *harmonic[4];
  GtkWidget *detune[4];
  GtkWidget *intonation[4][2];


  GtkWidget *velocityfollow[4];
  // key bias
  GtkWidget *breakpoint[4];    // [op]
  GtkWidget *keybias[4][2];    // [op][hi/lo value]
  // env keyfollow
  GtkWidget *keyfollow[4][2]; // [op][attack/sustain-loop]
  // key velocity
  GtkWidget *velocity[4]; // [op]

  GtkWidget *phase[4][2]; // [op][offset/velocityfollow]

  GtkWidget *temperament[7];
  GtkWidget *shruti[10];

  GtkWidget *common_spinbutton;
  GtkWidget *common_spinlabel;
  GtkWidget *common_oplabel;

  GtkWidget *monobutton;
  GtkWidget *lfo[6];
  GtkWidget *lfo_button[2];


  GtkWidget *window;
  GtkWidget *table[4];
  GtkWidget *canvas;
  int op;

}ed;


typedef struct
{
  short  pm[4];
  short  am[4];
  short mix[2];
  short  env_level[8];
  short  env_time[8];
  short harmonic;
  short intonation[2];
  short detune;
  short velocityfollow;
  unsigned char od;
  unsigned char button;
  short breakpoint;
  short keybias[2];
  short keyfollow[2];
  short velocity;
  short phase[2];
}mx44op_copypaste_buf;

static mx44op_copypaste_buf mx44op_buf;

/* Helper: set range value safely */
static
void set_value(GtkRange* range,double value)
{
  if(range)
    {
      GtkAdjustment *adj = gtk_range_get_adjustment(range);
      gtk_adjustment_set_value(adj,value);
    }

}

/* Forward declarations */
static void set_widgets(Mx44patch *tmp_patch,int channel ,int patchNumber);

/* Update UI widgets to reflect a patch */
static
void set_widgets(Mx44patch *tmp_patch,int channel ,int patchNumber)
{
  int i, op;

  patch_change = 1;

  if(midichannel == -1)
    {
      midichannel = channel;
      if (midichannel_spinner_adj)
        gtk_adjustment_set_value(midichannel_spinner_adj, midichannel+1);
    }

  tmp_patch = tmp_patch + channel;

   if( patchNumber != -1)
     {
      group = patchNumber >= 64;
      bank  = (patchNumber & 0x03F)>>3;
      patch = patchNumber & 0x07;

      if (bank_entry)
        gtk_combo_box_text_set_active(bank_entry, bank);

      if (patch_entry)
        gtk_combo_box_text_set_active(patch_entry, patch);

      if(group)
        {
          if (patch_group_2)
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(patch_group_2), TRUE);
        }
      else
        {
          if (patch_group_1)
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(patch_group_1), TRUE);
        }
    }

   if (patchname)
     g_object_set(patchname, "text", tmp_patch->name, NULL);

   for(op = 0; op < 4; ++op)
    {
      for(i=0; i < 4; ++i)
        {
          set_value((GtkRange*)ed.pm[op][i] , tmp_patch->pm[op][i]/320.0);
          set_value((GtkRange*)ed.am[op][i] , tmp_patch->am[op][i]/320.0);
        }
      set_value((GtkRange*)ed.mix[op][0] , tmp_patch->mix[op][0]/320.0);
      set_value((GtkRange*)ed.mix[op][1] , tmp_patch->mix[op][1]/177.0);

      for(i=0; i < 8; ++i)
        {
          set_value((GtkRange*)ed.env_level[op][i] , tmp_patch->env_level[op][i]/80.0);
          set_value((GtkRange*)ed.env_time[op][i] , tmp_patch->env_time[op][i]/320.0);
        }

      if (ed.od[op][tmp_patch->od[op]&0x07])
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (ed.od[op][tmp_patch->od[op]&0x07]), TRUE);

      set_value((GtkRange*)ed.harmonic[op] , tmp_patch->harmonic[op]/128.0);
      set_value((GtkRange*)ed.detune[op] , tmp_patch->detune[op]/100.0);
      set_value((GtkRange*)ed.intonation[op][0] , tmp_patch->intonation[op][0]/327.6);
      set_value((GtkRange*)ed.intonation[op][1] , tmp_patch->intonation[op][1]/327.6);

      set_value((GtkRange*)ed.velocityfollow[op] , tmp_patch->velocityfollow[op]/320.0);
      set_value((GtkRange*)ed.breakpoint[op] , tmp_patch->breakpoint[op]);
      /* keybias values are stored as squared values in the patch; UI expects sqrt */
      set_value((GtkRange*)ed.keybias[op][1] , -sqrtf((float)tmp_patch->keybias[op][0]));
      set_value((GtkRange*)ed.keybias[op][0] , -sqrtf((float)tmp_patch->keybias[op][1]));
      set_value((GtkRange*)ed.keyfollow[op][0] , tmp_patch->keyfollow[op][0]/320.0);
      set_value((GtkRange*)ed.keyfollow[op][1] , tmp_patch->keyfollow[op][1]/320.0);
      set_value((GtkRange*)ed.velocity[op] , tmp_patch->velocity[op]/320.0);
      set_value((GtkRange*)ed.phase[op][0] , tmp_patch->phase[op][0]/320.0);
      set_value((GtkRange*)ed.phase[op][1] , tmp_patch->phase[op][1]/320.0);

      if(tmp_patch->button[op]&WAWEBUTTON)
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (ed.wawebutton[op]), TRUE);
      else
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (ed.wawebutton[op]), FALSE);

      if(tmp_patch->button[op]&WHEELBUTTON)
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (ed.wheelbutton[op][0]), TRUE);
      else
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (ed.wheelbutton[op][0]), FALSE);

      if(tmp_patch->button[op]&LOWPASSBUTTON)
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (ed.lowpassbutton[op]), TRUE);
      else
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (ed.lowpassbutton[op]), FALSE);

      if(tmp_patch->button[op]&WAWESHAPEBUTTON)
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (ed.waweshapebutton[op]), TRUE);
      else
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (ed.waweshapebutton[op]), FALSE);

      if(tmp_patch->button[op]&PHASEFOLLOWKEYBUTTON)
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (ed.phasefollowkeybutton[op]), TRUE);
      else
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (ed.phasefollowkeybutton[op]), FALSE);

      if(tmp_patch->button[op]&MAGICBUTTON)
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (ed.magicbutton[op]), TRUE);
      else
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (ed.magicbutton[op]), FALSE);

      if(tmp_patch->button[op]&EXPRESSIONBUTTON)
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (ed.expressionbutton[op]), TRUE);
      else
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (ed.expressionbutton[op]), FALSE);

    }
   for(i = 0;i<6;++i)
     set_value((GtkRange*)ed.lfo[i] , tmp_patch->lfo[i]/320.0);

   for(i = 0;i<2;++i)
    if(tmp_patch->lfo_button[i])
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (ed.lfo_button[i]), TRUE);
    else
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (ed.lfo_button[i]), FALSE);

   if(mx44->monomode[channel])
     gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (ed.monobutton), TRUE);
   else
     gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (ed.monobutton), FALSE);

   patch_change = 0;
}

void setwidgets(Mx44patch *tmp_patch,int channel ,int patchNumber)
{
  if(channel == midichannel)
    {
      newpatch.tmp = tmp_patch;
      newpatch.channel = channel;
    }
}

static
int check_patch(void* data)
{
  if(mx44->patchNo[midichannel] != newpatch.number)
    {
      newpatch.number = mx44->patchNo[midichannel];
      set_widgets(newpatch.tmp, newpatch.channel, newpatch.number);
    }
  return 1;
}

/* unique widget names - keep simple unique naming */
static char * name_n()
{
  static int n =0;
  char *ret = g_strdup_printf("widget_%d", n++);
  return ret;
}

/* create a horizontal or vertical separator */
static
void line(GtkWidget *grid,int left,int top,int width,int height)
{
  char*name;
  GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
  name = name_n();
  gtk_widget_set_name (sep, name);
  g_object_ref (sep);
  g_object_set_data_full (G_OBJECT (ed.window), name, sep,
                            (GDestroyNotify) g_object_unref);
  gtk_widget_set_visible (sep, TRUE);
  gtk_grid_attach (GTK_GRID(grid), sep, left, top, width, height);
}

static char * label_font = "Sans 9";

static
GtkWidget *label(GtkWidget *grid,int left,int top,int width,char*text)
{
  GtkWidget *label = gtk_label_new (text);
  /* gtk_widget_override_font removed in GTK4; skip explicit font override here. */

  name_n();
  gtk_widget_set_name (label, text);
  g_object_ref (label);
  g_object_set_data_full (G_OBJECT (grid), text, label,
                            (GDestroyNotify) g_object_unref);
  gtk_widget_set_visible (label, TRUE);

  gtk_grid_attach (GTK_GRID (grid), label, left, top, width, 1);

  gtk_widget_set_sensitive (label, FALSE);
  gtk_label_set_justify (GTK_LABEL (label), GTK_JUSTIFY_LEFT);
  gtk_label_set_xalign (GTK_LABEL (label), 0.0);

  return label;
}

/* Focus handlers show value in the common spin */
static
void  on_focus_range(GtkRange *range, GdkEvent *event, hint_t* hint)
{
  GtkAdjustment *adj  = gtk_range_get_adjustment(range);
  if(patch_change)
    return;
  if(spin_adj != adj)
    {
      spin_adj = adj;
      gtk_label_set_text((GtkLabel*)ed.common_spinlabel,hint->spinlabel);
      if(hint->op <0)
        gtk_label_set_text((GtkLabel*)ed.common_oplabel,"Mx44");
      else
        gtk_label_set_text((GtkLabel*)ed.common_oplabel,op_label[hint->op]);
      if(hint->op >= -1 && ed.common_spinbutton)
      {
        gtk_spin_button_set_adjustment(GTK_SPIN_BUTTON(ed.common_spinbutton), adj);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(ed.common_spinbutton), gtk_adjustment_get_value(adj));
      }
    }
}

/* value changed on adjustments */
static
void on_value_changed( GtkAdjustment *adj, hint_t* hint)
{
  double value = gtk_adjustment_get_value(adj);
  if(patch_change)
    return;
  if(spin_adj != adj)
    {
      spin_adj = adj;
    }

  switch(hint->group)
    {
    case MISC:
      if(hint->index == 0) // phase init
        {
          mx44tmpPatch[ midichannel ].phase[hint->op][0]
            = value * 320 ;
        }
      else if(hint->index == 1) // phase velocity
        {
          mx44tmpPatch[ midichannel ].phase[hint->op][1]
            = value * 320 ;
        }
      else if(hint->index == 2) // velocity sensitivity
        {
          mx44tmpPatch[ midichannel ].velocity[hint->op]
            = value * 320 ;
        }
      else if(hint->index == 3) // output volume
        {
          mx44tmpPatch[ midichannel ].mix[hint->op][0]
            = value * 320 ;
        }
      else if(hint->index == 4)  // output balance
        {
          mx44tmpPatch[ midichannel ].mix[hint->op][1]
            = value * 177 ;
        }
      break;
    case ENV1: // envelope time
      {
        mx44tmpPatch[ midichannel ].env_time[hint->op][hint->index]
          = value * 320 ;
        break;
      }
    case ENV2: // envelope level
      {
        mx44tmpPatch[ midichannel ].env_level[hint->op][hint->index]
          = value * 80;
        break;
      }
    case ENV3:
      if(hint->index == 0) // attacktime velocityfollow
        {
          mx44tmpPatch[ midichannel ].velocityfollow[hint->op]
            = value * 320 ;
        }
      else if(hint->index == 1) // attack/release keyfollow
        {
          mx44tmpPatch[ midichannel ].keyfollow[hint->op][0]
            = value * 320 ;
        }
      else if(hint->index == 2) // sustainloop keyfollow
        {
          mx44tmpPatch[ midichannel ].keyfollow[hint->op][1]
            = value * 320 ;
        }

      break;
    case FREQ:
      if(hint->index == 0) // frequency course
        {
          mx44tmpPatch[ midichannel ].harmonic[hint->op]
            = value * 128 ;
        }
      else if(hint->index == 2) // frequency offset
        {
          mx44tmpPatch[ midichannel ].detune[hint->op]
            = value * 100;
        }
      else if(hint->index == 3) // intonation amount
        {
          mx44tmpPatch[ midichannel ].intonation[hint->op][0]
            = value * 327.6 ;
        }
      else if(hint->index == 4) // intonation decay
        {
          mx44tmpPatch[ midichannel ].intonation[hint->op][1]
            = value * 327.6 ;
        }
      break;
    case BIAS:
      if(hint->index == 0) // breakpoint
        {
          mx44tmpPatch[ midichannel ].breakpoint[hint->op]
            = value;
        }
      else if(hint->index == 1) // bias low
        {
          mx44tmpPatch[ midichannel ].keybias[hint->op][1]
            = value * value;
        }
      else if(hint->index == 2) // bias low
        {
          mx44tmpPatch[ midichannel ].keybias[hint->op][0]
            = value * value;
        }
      break;
    case MOD1:
      mx44tmpPatch[ midichannel ].pm[hint->op][hint->index]
        = value * 320;
      break;
    case MOD2:
      mx44tmpPatch[ midichannel ].am[hint->op][hint->index]
        = value * 320;
      break;
    case DELAY:
      puts("no delay yet");
      break;
    case DIST:
      puts("no overdrive yet");
      break;
    case LFO:
      mx44tmpPatch[ midichannel ].lfo[hint->index]
        = value * 320;
      break;
    }

    if (copied && hint->group != DELAY && hint->group != DIST && hint->group != LFO)
    {
      gtk_widget_set_sensitive((GtkWidget*)ed.copybutton[hint->op], TRUE);
      gtk_widget_set_sensitive((GtkWidget*)ed.pastebutton[hint->op], TRUE);
    }
}

/* scale factory - creates a GtkScale (horizontal or vertical) attached to a grid */
static
GtkWidget *scale(GtkWidget *grid,int left,int top,int width,int height,
                  float min, float max,int op,int group,int groupindex,char*help)
{
  GtkAdjustment *adj;
  GtkWidget *widget;
  hint_t *hint = g_new0(hint_t,1);
  char*name = name_n();

  hint->spinlabel=help ? g_strdup(help) : g_strdup("");
  hint->op = op;
  hint->group = group;
  hint->index = groupindex;
  hint->min = min;
  hint->max = max;

  adj = gtk_adjustment_new (0, min, max, 0.1, 1.0, 0.0);

  g_signal_connect (adj, "value_changed",
                    G_CALLBACK(on_value_changed), hint);

  /* choose orientation based on width/height like original file */
  if(width > height)
    widget = gtk_scale_new (GTK_ORIENTATION_HORIZONTAL, adj);
  else
    {
      widget = gtk_scale_new (GTK_ORIENTATION_VERTICAL, adj);
      gtk_range_set_inverted (GTK_RANGE(widget), TRUE);
    }

  /* add a focus event controller to mimic hover/focus behavior */
  GtkEventController *ec = gtk_event_controller_focus_new();
  /* connect enter (focus) to set common spin label */
  g_signal_connect (ec, "enter", G_CALLBACK(on_focus_range), hint);
  gtk_widget_add_controller(widget, ec);

  gtk_widget_set_name (widget,name);
  g_object_ref (widget);
  g_object_set_data_full (G_OBJECT (ed.window),name, widget,
                            (GDestroyNotify) g_object_unref);
  gtk_widget_set_visible (widget, TRUE);

  gtk_grid_attach (GTK_GRID(grid), widget, left, top, width, height);

  gtk_scale_set_draw_value (GTK_SCALE (widget), FALSE);

  return widget;
}

/* Envelope widget creation (uses grid) */
static
void mk_envelope(GtkWidget *grid,int left,int top,int op,char*name)
{
  int h = 8;
  int w = 0;
  int i;
  int index = 0;
  int size = 6;
  char*time_help[]={"  attack time 1","  attack time 2","  attack time 3","  attack time 4",
                    "  sustainloop time 1","  sustainloop time 2",
                    "  release time 1","  release time 2"};
  char*level_help[]={"  startlevel","  attack level 1","  attack level 2","  attack level 3",
                     "  sustainloop level 1","  sustainloop level 2",
                     "  release level 1","  release level 2"};

  for(i = 0;i<4;++i,--h,++w)
    {
      ed.env_time[op][index] = scale(grid,left+i+1,top+h,size-1,1,
                                     0,100,op,ENV1,index,time_help[index]);
      if(i)
        ed.env_level[op][index] = scale(grid,left+i+size-1,top+h+1,1,size -2,
                                        0,100,op,ENV2,index,level_help[index]);
      else
        ed.env_level[op][index] = scale(grid,left,top+h,1,size -2,
                                        0,100,op,ENV2,index,level_help[index]);

      index++;
    }

  label(grid,h-2,top+i,size+6,name);

  while(h-->0)
    {
      ++i;
      ed.env_time[op][index] = scale(grid,left+i+1,top+h,size-1,1,
                                     0,100,op,ENV1,index,time_help[index ]);

      if(h>1)
        ed.env_level[op][index] = scale(grid,left+i+size-1,top+h+1,1,size -2,
                                        0,100,op,ENV2,index,level_help[index ]);
      else if(h == 1)
        ed.env_level[op][index+1] = scale(grid,left+i+size,top+h,1,size -2,
                                          0,100,op,ENV2,index+1,level_help[index ]);
      index++;
    }

  ed.velocityfollow[op] = scale(grid,
                  6+left,12+top,3,1,
                  0,100,op,ENV3,0,"  attacktime velocityfollow");
  label(ed.table[op],9+left,10+top,5,"Keyfollow");
  ed.keyfollow[op][0] = scale(grid,
                9+left,9+top,3,1,
                0,100,op,ENV3,1,"  attack/release keyfollow");
  ed.keyfollow[op][1] = scale(grid,
                10+left,8+top,3,1,
                0,100,op,ENV3,2,"  sustainloop keyfollow");
}

/* Patchbay (pm/am) creation */
static
void mk_patchbay(GtkWidget *grid,int left,int top,int op,char*name)
{
  int i;
  char*am_help[]={"  op 1 amplitude modulation","  op 2 amplitude modulation",
                  "  op 3 amplitude modulation","  op 4 amplitude modulation"};
  char*pm_help[]={"  op 1 phase modulation","  op 2 phase modulation",
                  "  op 3 phase modulation","  op 4 phase modulation"};
  for(i = 0;i<8;++i)
    {
      int x2=0;
      if(i&2) x2 = 2;

      if(i&1)
        {
          ed.am[op][i>>1] = scale(grid,left+x2,top+i,4,1,
                                  -100,100,op,MOD2,i>>1,am_help[i>>1]);
        }
      else
        {
          GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_VERTICAL);
          char *n = name_n();
          gtk_widget_set_name (sep, n);
          g_object_ref (sep);
          g_object_set_data_full (G_OBJECT (ed.window), n, sep,
                                  (GDestroyNotify) g_object_unref);
          gtk_widget_set_visible (sep, TRUE);
          gtk_grid_attach (GTK_GRID (grid), sep, left+x2+1, top+i-(i==0), 2, 2);

          ed.pm[op][i>>1] = scale(grid,left+x2,top+i,4,1,
                                  -100,100,op,MOD1,i>>1,pm_help[i>>1]);
        }
    }
  label(grid,left,top+2,3," fm");
  label(grid,left,top+3,3," am");
}

static
GtkWidget* tab_label(GtkWidget *window,char *name)
{
  GtkWidget *label;
  label = gtk_label_new (name);
  g_object_ref (label);
  g_object_set_data_full (G_OBJECT (window), name_n(), label,
                            (GDestroyNotify) g_object_unref);
  gtk_widget_set_visible (label, TRUE);
  return label;
}

/* ---------- Callbacks (rest) ---------- */

static
void on_od_clicked (GtkButton *button,
                    void*  user_data)
{
  int value = ((intptr_t)user_data)&0x0F;
  int op = ((intptr_t)user_data)>>4;
  mx44tmpPatch[ midichannel ].od[op] = value;
  gtk_widget_set_sensitive((GtkWidget*)ed.copybutton[op], TRUE);
  if (copied)
      gtk_widget_set_sensitive((GtkWidget*)ed.pastebutton[op], TRUE);
}

static
void on_temperament_clicked (GtkButton *button,
                             void*  user_data)
{
  int value = (int)(intptr_t)user_data;
  mx44->temperament = value;
}

static
void on_shruti_button_clicked (GtkToggleButton *button,
                               void*  user_data)
{
  int data = (int)(intptr_t)user_data;
  int tmp = 0;
  if(gtk_toggle_button_get_active((GtkToggleButton*)ed.shruti[data]))
    tmp = 1;

  data +=1;
  data += (data > 6);
  mx44->shruti[data] = tmp;
}

static
void on_lfo_button_clicked (GtkToggleButton *button,
                            void*  user_data)
{
  if(button == (GtkToggleButton*)ed.lfo_button[0])
    mx44tmpPatch[ midichannel ].lfo_button[0] = gtk_toggle_button_get_active(button);
  else if(button == (GtkToggleButton*)ed.lfo_button[1])
    mx44tmpPatch[ midichannel ].lfo_button[1] = gtk_toggle_button_get_active(button);
}

static
void on_button_clicked (GtkToggleButton *button,
                        void*  user_data)
{
  int op = ((intptr_t)user_data)&0x0F;
  int number = ((intptr_t)user_data)>>4;
  char b = mx44tmpPatch[ midichannel ].button[op];

  if(gtk_toggle_button_get_active(button))
    {
      mx44tmpPatch[ midichannel ].button[op] = b | number;
    }
  else
    {
      mx44tmpPatch[ midichannel ].button[op] = b &(~number);
    }
  gtk_widget_set_sensitive((GtkWidget*)ed.copybutton[op], TRUE);
  if (copied)
    gtk_widget_set_sensitive((GtkWidget*)ed.pastebutton[op], TRUE);
}

static
void  patch_changed(void)
{
  int midipatch = group*64+bank*8+patch;
  if(midipatch < 0 || midipatch > 127)
    return;
  mx44patchNo [midichannel] = midipatch;
  newpatch.number = midipatch;
  if(patch_change)
    return;

  if(!saving)
    if(midichannel != -1)
      {
        mx44tmpPatch[midichannel] = mx44patch[midipatch];
        set_widgets(mx44tmpPatch,midichannel,-1);
      }
}

static
void on_patch_group_1_clicked (GtkButton *button,
                               void* user_data)
{
  group = 0;
  patch_changed();
  int i;
  for (i = 0; i < 4; ++i)
  {
    gtk_widget_set_sensitive((GtkWidget*)ed.copybutton[i],  TRUE);
    if (copied)
        gtk_widget_set_sensitive((GtkWidget*)ed.pastebutton[i], TRUE);
  }
}

static
void on_patch_group_2_clicked (GtkButton *button,
                               void* user_data)
{
  group = 1;
  patch_changed();
  int i;
  for (i = 0; i < 4; ++i)
  {
    gtk_widget_set_sensitive((GtkWidget*)ed.copybutton[i],  TRUE);
    if (copied)
        gtk_widget_set_sensitive((GtkWidget*)ed.pastebutton[i], TRUE);
  }
}

static
int on_bank_entry_changed (GtkComboBoxText *combo,
                           void* user_data)
{
  bank = gtk_combo_box_get_active(GTK_COMBO_BOX((GTK_COMBO_BOX_TEXT(combo))));
  patch_changed();
  return 0;
}

static
int on_patch_entry_changed (GtkComboBoxText *combo,
                            void* user_data)
{
  patch = gtk_combo_box_get_active(GTK_COMBO_BOX((GTK_COMBO_BOX_TEXT(combo))));
  patch_changed();
  return 0;
}

static
void on_save_button_toggled (GtkToggleButton *togglebutton,
                             void* user_data)
{

  if(gtk_toggle_button_get_active(togglebutton))
    saving = TRUE;
  else
    {
      if(savebutton)
        {
          printf("patch: %i channel %i\n",mx44patchNo [midichannel] ,midichannel);
          /* Safely read the entry text via g_object_get and copy into fixed buffer */
          {
            char *txt = NULL;
            g_object_get(patchname, "text", &txt, NULL);
            if (txt) {
              strncpy(mx44tmpPatch[midichannel].name, txt, sizeof(mx44tmpPatch[midichannel].name)-1);
              mx44tmpPatch[midichannel].name[sizeof(mx44tmpPatch[midichannel].name)-1] = '\0';
              g_free(txt);
            } else {
              mx44tmpPatch[midichannel].name[0] = '\0';
            }
          }
          mx44patch[(unsigned)mx44patchNo[midichannel]] = mx44tmpPatch[midichannel];
        }

      saving = FALSE;
    }

  savebutton = togglebutton;

}

static
void on_copy_button_pressed (GtkButton * button,
                              void * user_data)
{
    if (midichannel == -1)
        return;
    int op = ((intptr_t)user_data)>>4;

    int i;

    for (i = 0; i < 8; ++i)
    {
        mx44op_buf.env_level[i] = mx44tmpPatch[midichannel].env_level[op][i];
        mx44op_buf.env_time[i] =  mx44tmpPatch[midichannel].env_time[op][i];
    }
    for (i = 0; i < 4; ++i)
    {
        mx44op_buf.pm[i] = mx44tmpPatch[midichannel].pm[op][i];
        mx44op_buf.am[i] = mx44tmpPatch[midichannel].am[op][i];
    }
    for (i = 0; i < 2; ++i)
    {
        mx44op_buf.mix[i] =        mx44tmpPatch[midichannel].mix[op][i];
        mx44op_buf.intonation[i] = mx44tmpPatch[midichannel].intonation[op][i];
        mx44op_buf.keybias[i] =    mx44tmpPatch[midichannel].keybias[op][i];
        mx44op_buf.keyfollow[i] =  mx44tmpPatch[midichannel].keyfollow[op][i];
        mx44op_buf.phase[i] =      mx44tmpPatch[midichannel].phase[op][i];
    }
    mx44op_buf.harmonic =       mx44tmpPatch[midichannel].harmonic[op];
    mx44op_buf.detune =         mx44tmpPatch[midichannel].detune[op];
    mx44op_buf.velocityfollow = mx44tmpPatch[midichannel].velocityfollow[op];
    mx44op_buf.od =             mx44tmpPatch[midichannel].od[op];
    mx44op_buf.button =         mx44tmpPatch[midichannel].button[op];
    mx44op_buf.breakpoint =     mx44tmpPatch[midichannel].breakpoint[op];
    mx44op_buf.velocity =       mx44tmpPatch[midichannel].velocity[op];

    gtk_widget_set_sensitive((GtkWidget*)button, FALSE);

    for (i = 0; i < 4; ++i)
    {
        if (i == op)
            gtk_widget_set_sensitive((GtkWidget*)ed.pastebutton[op],FALSE);
        else
        {
            gtk_widget_set_sensitive((GtkWidget*)ed.pastebutton[i], TRUE);
            gtk_widget_set_sensitive((GtkWidget*)ed.copybutton[i],  TRUE);
        }
    }
    copied = 1;
}

static
void on_paste_button_pressed (GtkButton * button,
                               void * user_data)
{
    if (midichannel == -1)
        return;
    int op = ((intptr_t)user_data)>>4;
    int i;
    for (i = 0; i < 8; ++i)
    {
        mx44tmpPatch[midichannel].env_level[op][i] = mx44op_buf.env_level[i];
        mx44tmpPatch[midichannel].env_time[op][i]  = mx44op_buf.env_time[i];
    }
    for (i = 0; i < 4; ++i)
    {
        mx44tmpPatch[midichannel].pm[op][i] = mx44op_buf.pm[i];
        mx44tmpPatch[midichannel].am[op][i] = mx44op_buf.am[i];
    }
    for (i = 0; i < 2; ++i)
    {
        mx44tmpPatch[midichannel].mix[op][i] =        mx44op_buf.mix[i];
        mx44tmpPatch[midichannel].intonation[op][i] = mx44op_buf.intonation[i];
        mx44tmpPatch[midichannel].keybias[op][i] =    mx44op_buf.keybias[i];
        mx44tmpPatch[midichannel].keyfollow[op][i] =  mx44op_buf.keyfollow[i];
        mx44tmpPatch[midichannel].phase[op][i] =      mx44op_buf.phase[i];
    }
    mx44tmpPatch[midichannel].harmonic[op] =       mx44op_buf.harmonic;
    mx44tmpPatch[midichannel].detune[op] =         mx44op_buf.detune;
    mx44tmpPatch[midichannel].velocityfollow[op] = mx44op_buf.velocityfollow;
    mx44tmpPatch[midichannel].od[op] =             mx44op_buf.od;
    mx44tmpPatch[midichannel].button[op] =         mx44op_buf.button;
    mx44tmpPatch[midichannel].breakpoint[op] =     mx44op_buf.breakpoint;
    mx44tmpPatch[midichannel].velocity[op] =       mx44op_buf.velocity;

    set_widgets(mx44tmpPatch,midichannel,mx44patchNo[midichannel]);

    gtk_widget_set_sensitive((GtkWidget*)ed.copybutton[op],  FALSE);
    gtk_widget_set_sensitive((GtkWidget*)ed.pastebutton[op], FALSE);

}

static
void on_esc_save_button_pressed (GtkButton *button,
                 void* user_data)
{
  GtkToggleButton *sb = savebutton;
  savebutton = NULL;
  if(sb)
    gtk_toggle_button_set_active(sb,FALSE);
}

static
void on_monobutton_toggled (GtkToggleButton *togglebutton,
                 void* user_data)
{
  mx44->monomode[midichannel] = gtk_toggle_button_get_active(togglebutton);
}

/* Create combo helper and UI builder */
static GtkComboBoxText* create_combo_text_with_items(const char **items, int n)
{
  GtkComboBoxText *combo = GTK_COMBO_BOX_TEXT(gtk_combo_box_text_new());
  for (int i=0;i<n;++i)
    gtk_combo_box_text_append_text(combo, items[i]);
  gtk_combo_box_set_active(GTK_COMBO_BOX(combo), 0);
  return combo;
}

/* Create the main window and widgets. Returns the window. */
static
GtkWindow* create_window (gboolean has_rc)
{
  GtkWidget  *frame,*basetable = NULL;

  ed.window = gtk_window_new();
  gtk_window_set_title(GTK_WINDOW(ed.window), "Mx44.2");

  window1=ed.window;
  char *name = name_n();
  gtk_widget_set_name (ed.window, name);
  g_object_set_data (G_OBJECT (ed.window), name, ed.window);

  basetable = gtk_grid_new();
  g_object_ref (basetable);
  gtk_widget_set_visible(basetable, TRUE);

  /* patch table area using a grid */
  GtkWidget *patch_table = gtk_grid_new();
  g_object_ref (patch_table);
  g_object_set_data_full (G_OBJECT (window1), "patch_table", patch_table,
              (GDestroyNotify) g_object_unref);
  gtk_widget_set_visible (patch_table, TRUE);

  gtk_grid_attach (GTK_GRID (basetable),
          patch_table, 0,1, 3,1);

  /* patch group and controls (abbreviated, following prior layout) */
  patch_group_1 = gtk_toggle_button_new();
  g_object_ref (patch_group_1);
  g_object_set_data_full (G_OBJECT (window1), "patch_group_1", patch_group_1,
              (GDestroyNotify) g_object_unref);
  gtk_widget_set_visible (patch_group_1, TRUE);
  gtk_grid_attach (GTK_GRID (patch_table), patch_group_1, 1,0, 2,1);
  gtk_widget_set_tooltip_text (patch_group_1, "patch group 1");

  patch_group_2 = gtk_toggle_button_new();
  g_object_ref (patch_group_2);
  g_object_set_data_full (G_OBJECT (window1), "patch_group_2", patch_group_2,
              (GDestroyNotify) g_object_unref);
  gtk_widget_set_visible (patch_group_2, TRUE);
  gtk_grid_attach (GTK_GRID (patch_table), patch_group_2, 3,0, 2,1);
  gtk_widget_set_tooltip_text (patch_group_2, "patch group 2");

  bank_entry = create_combo_text_with_items((const char*[]){"A","B","C","D","E","F","G","H"}, 8);
  g_object_ref(bank_entry);
  g_object_set_data_full (G_OBJECT (window1), "bank_combo", bank_entry,
              (GDestroyNotify) g_object_unref);
  gtk_widget_set_tooltip_text (GTK_WIDGET(bank_entry), "bank selection");
  gtk_widget_set_visible(GTK_WIDGET(bank_entry), TRUE);
  gtk_grid_attach (GTK_GRID (patch_table), GTK_WIDGET(bank_entry), 5,0, 4,1);

  patch_entry = create_combo_text_with_items((const char*[]){"1","2","3","4","5","6","7","8"}, 8);
  g_object_ref(patch_entry);
  g_object_set_data_full (G_OBJECT (window1), "patch_combo", patch_entry,
              (GDestroyNotify) g_object_unref);
  gtk_widget_set_tooltip_text(GTK_WIDGET(patch_entry), "patch selection");
  gtk_widget_set_visible(GTK_WIDGET(patch_entry), TRUE);
  gtk_grid_attach (GTK_GRID (patch_table), GTK_WIDGET(patch_entry), 9,0, 4,1);

  patchname = gtk_entry_new();
  gtk_entry_set_max_length(GTK_ENTRY(patchname),31);
  g_object_ref (patchname);
  g_object_set_data_full (G_OBJECT (window1), "patchname", patchname,
              (GDestroyNotify) g_object_unref);
  gtk_widget_set_visible (patchname, TRUE);
  gtk_grid_attach (GTK_GRID (patch_table), patchname, 13,0, 14,1);

  GtkWidget *save_button = gtk_toggle_button_new_with_label ("SAVE");
  g_object_ref (save_button);
  g_object_set_data_full (G_OBJECT (window1), "save_button", save_button,
              (GDestroyNotify) g_object_unref);
  gtk_widget_set_visible (save_button, TRUE);
  gtk_grid_attach (GTK_GRID (patch_table), save_button, 27,0, 4,1);
  gtk_widget_set_tooltip_text (save_button, "select patch to save in, then release button");

  GtkWidget *esc_save_button = gtk_button_new_with_label ("Esc");
  g_object_ref (esc_save_button);
  g_object_set_data_full (G_OBJECT (window1), "esc_save_button", esc_save_button,
              (GDestroyNotify) g_object_unref);
  gtk_widget_set_visible (esc_save_button, TRUE);
  gtk_grid_attach (GTK_GRID (patch_table), esc_save_button, 31,0, 3,1);
  gtk_widget_set_tooltip_text (esc_save_button, "cancel save procedure");

  ed.common_oplabel = tab_label(ed.window, "Mx44");
  gtk_label_set_justify (GTK_LABEL (ed.common_oplabel), GTK_JUSTIFY_LEFT);
  gtk_widget_set_size_request (ed.common_oplabel ,20, -1);
  gtk_grid_attach (GTK_GRID (patch_table), ed.common_oplabel, 36,0, 3,1);

  ed.common_spinbutton = gtk_spin_button_new(NULL, 1.0, 2);
  g_object_ref(ed.common_spinbutton);
  gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(ed.common_spinbutton), TRUE);
  gtk_widget_set_size_request (ed.common_spinbutton ,50, -1);
  gtk_widget_set_visible (ed.common_spinbutton, TRUE);
  gtk_grid_attach (GTK_GRID (patch_table), ed.common_spinbutton, 39,0, 5,1);

  ed.common_spinlabel = tab_label(ed.window, "  (C) Jens M Andreasen, 2009");
  gtk_label_set_justify (GTK_LABEL (ed.common_spinlabel), GTK_JUSTIFY_LEFT);
  gtk_widget_set_size_request (ed.common_spinlabel ,60, -1);
  gtk_grid_attach (GTK_GRID (patch_table), ed.common_spinlabel, 44,0, 15,1);

  ed.monobutton = gtk_toggle_button_new_with_label ("M");
  g_object_ref (ed.monobutton);
  g_object_set_data_full (G_OBJECT (window1), "monobutton", ed.monobutton,
              (GDestroyNotify) g_object_unref);
  gtk_widget_set_visible(ed.monobutton, TRUE);
  gtk_grid_attach (GTK_GRID (patch_table), ed.monobutton, 59,0, 2,1);

  GtkComboBoxText *ch_combo = create_combo_text_with_items((const char*[]){
      "Ch 1","Ch 2","Ch 3","Ch 4","Ch 5","Ch 6","Ch 7","Ch 8",
      "Ch 9","Ch10","Ch11","Ch12","Ch13","Ch14","Ch15","Ch16"}, 16);
  g_object_ref (ch_combo);
  g_object_set_data_full (G_OBJECT (window1), "ch_combo", ch_combo,
              (GDestroyNotify) g_object_unref);
  gtk_widget_set_visible(GTK_WIDGET(ch_combo), TRUE);
  gtk_grid_attach (GTK_GRID (patch_table), GTK_WIDGET(ch_combo), 61,0, 4,1);
  g_signal_connect (ch_combo, "changed", G_CALLBACK (on_ch_combo), NULL);

  g_signal_connect (bank_entry, "changed", G_CALLBACK (on_bank_entry_changed), NULL);
  g_signal_connect (patch_entry, "changed", G_CALLBACK (on_patch_entry_changed), NULL);
  g_signal_connect (save_button, "toggled", G_CALLBACK (on_save_button_toggled), NULL);
  g_signal_connect (esc_save_button, "clicked", G_CALLBACK (on_esc_save_button_pressed), NULL);
  g_signal_connect (ed.monobutton, "toggled", G_CALLBACK (on_monobutton_toggled), NULL);
  g_signal_connect (patch_group_1, "pressed", G_CALLBACK (on_patch_group_1_clicked), NULL);
  g_signal_connect (patch_group_2, "pressed", G_CALLBACK (on_patch_group_2_clicked), NULL);

  frame = gtk_frame_new(NULL);
  gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_NONE);
  gtk_widget_set_visible(frame, TRUE);

  scale_table = gtk_grid_new();
  gtk_widget_set_visible(scale_table, TRUE);
  g_object_ref(scale_table);
  g_object_set_data_full (G_OBJECT (window1), "scale_table", scale_table,
              (GDestroyNotify) g_object_unref);

  gtk_container_set_border_width(GTK_CONTAINER(frame), 0);
  gtk_container_add(GTK_CONTAINER(frame), scale_table);
  gtk_widget_set_visible(scale_table, TRUE);

  gtk_grid_attach (GTK_GRID (basetable),
          frame, 0,3, 2,1);

  frame = gtk_frame_new(NULL);
  gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_NONE);
  gtk_widget_set_visible(frame, TRUE);

  GtkWidget *fx_table = gtk_grid_new();
  g_object_ref (fx_table);
  g_object_set_data_full (G_OBJECT (window1), "fx_table", fx_table,
              (GDestroyNotify) g_object_unref);
  gtk_widget_set_visible(fx_table, TRUE);
  gtk_container_add(GTK_CONTAINER(frame), fx_table);
  gtk_grid_attach (GTK_GRID (basetable),
          frame, 2,0, 1,3);

  ed.lfo[0] = scale(fx_table, 1,12,1,4, 0,100,-1,LFO,0,"  LFO rate 1");
  ed.lfo[1] = scale(fx_table, 2,12,1,4, 0,100,-1,LFO,1,"  LFO rate 2");
  ed.lfo[2] = scale(fx_table, 4,10,1,4, 0,100,-1,LFO,2,"  LFO amount 1");
  ed.lfo[3] = scale(fx_table, 5,9,1,4, 0,100,-1,LFO,3,"  LFO amount 2");
  ed.lfo[4] = scale(fx_table, 0,9,5,1, 0,100,-1,LFO,4,"  LFO time 1");
  ed.lfo[5] = scale(fx_table, 1,8,5,1, 0,100,-1,LFO,5,"  LFO time 2");

  label(fx_table,1,10,4," Sync");
  label(fx_table,1,11,4," Wheel");

  GtkWidget *btn = gtk_check_button_new();
  g_object_ref(btn);
  g_object_set_data_full (G_OBJECT (window1), "loopbutton", btn,
              (GDestroyNotify) g_object_unref);
  g_signal_connect (btn, "clicked", G_CALLBACK (on_lfo_button_clicked), NULL);
  gtk_widget_set_visible (btn, TRUE);
  gtk_grid_attach (GTK_GRID (fx_table), btn, 0,10, 1,1);
  gtk_widget_set_size_request (btn, CSZ, CSZ);
  ed.lfo_button[0] = btn;

  btn = gtk_check_button_new();
  g_object_ref(btn);
  g_object_set_data_full (G_OBJECT (window1), "touchbutton", btn,
              (GDestroyNotify) g_object_unref);
  g_signal_connect (btn, "clicked", G_CALLBACK (on_lfo_button_clicked), GINT_TO_POINTER(1));
  gtk_widget_set_visible (btn, TRUE);
  gtk_grid_attach (GTK_GRID (fx_table), btn, 0,11, 1,1);
  gtk_widget_set_size_request (btn, CSZ, CSZ);
  ed.lfo_button[1] = btn;

  label(scale_table,0,0,1," Temperament ");
  int x = 1;
  for (int t=0;t<4;++t)
    {
      ed.temperament[t] = gtk_radio_button_new(NULL);
      gtk_widget_set_tooltip_text(ed.temperament[t], temp_tips[t]);
      gtk_widget_set_visible(ed.temperament[t], TRUE);
      gtk_grid_attach (GTK_GRID (scale_table), ed.temperament[t], x,0, 1,1);
      gtk_widget_set_size_request (ed.temperament[t], CSZ, CSZ);
      g_signal_connect (ed.temperament[t], "clicked", G_CALLBACK (on_temperament_clicked), GINT_TO_POINTER(t));
      if (t == mx44->temperament)
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ed.temperament[t]), TRUE);
      ++x;
    }
  x++;
  for (int t=0; t<10; ++t)
    {
      GtkWidget *gw;
      if (t == 0)
        {
          gw = label(scale_table, x, 0, 1, ":  D ");
          gtk_widget_set_tooltip_text (gw," tonica (SA)");
          gtk_widget_set_sensitive (gw, TRUE);
          ++x;
        }
      else if (t == 6)
        {
          gw = label(scale_table, x, 0, 1, " A ");
          gtk_widget_set_tooltip_text (gw," perfect fifth (PA)");
          gtk_widget_set_sensitive (gw, TRUE);
          ++x;
        }

      ed.shruti[t] = gtk_check_button_new();
      gtk_widget_set_visible(ed.shruti[t], TRUE);
      g_signal_connect (ed.shruti[t], "clicked", G_CALLBACK (on_shruti_button_clicked), GINT_TO_POINTER(t));
      if(t < 6)
        gtk_grid_attach (GTK_GRID (scale_table), ed.shruti[t], x, 0, 1, 1);
      else
        {
          gtk_grid_attach (GTK_GRID (scale_table), ed.shruti[t], x, 0, 2, 1);
          ++x;
        }
      gtk_widget_set_size_request (ed.shruti[t], CSZ, CSZ);
      gtk_widget_set_tooltip_text (ed.shruti[t], shruti_tips[t]);
      ++x;
    }

  gtk_window_set_child(GTK_WINDOW(ed.window), basetable);
  gtk_widget_set_visible(ed.window, TRUE);

  return GTK_WINDOW(ed.window);
}

/* Application activate handler: build UI and present window */
static void
app_activate(GApplication *app, gpointer user_data)
{
    create_window(TRUE);

    if (GTK_IS_WINDOW(ed.window))
        gtk_window_set_application(GTK_WINDOW(ed.window), GTK_APPLICATION(app));

    set_widgets(mx44tmpPatch,0,mx44patchNo[0]);

    int i,x;
    for(i = 0, x=0; i < 12; ++i)
      {
        if(i == 0|| i == 7)
          continue;
        if(mx44->shruti[i])
          gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (ed.shruti[x]), TRUE);
        ++x;
      }

    g_timeout_add (50, check_patch, NULL);

    if (GTK_IS_WINDOW(ed.window))
        gtk_window_present(GTK_WINDOW(ed.window));
}

/* ---------- Main entry used by the rest of the program ---------- */

int main_interface (int argc, char *argv[])
{
  mx44patch =   mx44->patch;
  mx44patchNo = mx44->patchNo;
  mx44tmpPatch = newpatch.tmp = mx44->tmpPatch;

  memset(&mx44op_buf, 0, sizeof(mx44op_copypaste_buf));

  GtkApplication *app;
  int status;

  app = gtk_application_new("org.mx44.app", G_APPLICATION_FLAGS_NONE);
  g_signal_connect(app, "activate", G_CALLBACK(app_activate), NULL);

  status = g_application_run(G_APPLICATION(app), argc, argv);
  g_object_unref(app);

  return status;
}