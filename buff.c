//#define GTK_DISABLE_DEPRECATED
/* buff.c
 *
 * window buffer implementation for the Xnmr project
 *
 * UBC Physics
 * April, 2000
 * 
 * written by: Carl Michal, Scott Nelson
 */




/*
  File saving and loading:



  file_save_as ->check_overwrite_wrapper     (save_as menu item)
                  |
                  |
  file_save ->  check_overwrite      ->    do_save   (save menu item)
                   \                     /
		     -> do_save_wrapper /

file_open  -> do_load_wrapper -> do_load       (open menu item)
                               /
reload -----------------------/                (reload button)
 /\
  |
reload wrapper                             (from end of acquisition)


*/



#include <unistd.h>
#include <string.h>
#include <gtk/gtk.h>
#include <sys/stat.h>  //for mkdir, etc
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <wordexp.h>

#include "buff.h"
#include "xnmr.h"
#include "xnmr_ipc.h"
#include "panel.h"
#include "param_f.h"
#include "p_signals.h"
#include "param_utils.h"
#include "nr.h"
/*
 *  Global Variable for this modules
 */

extern char no_acq;
extern char connected; //from xnmr_ipc.c
extern struct parameter_button_t param_button[ MAX_PARAMETERS ];
extern GtkObject *acqs_2d_adj;
extern char no_update_open;
extern struct popup_data_t popup_data; // needed to see if array is open 
GdkPoint dpoints[MAX_DATA_NPTS]; // point list for drawing spectra

volatile extern char redraw; 

int current;
int last_current;
int num_buffs;
int active_buff;
dbuff *buffp[MAX_BUFFERS];
int array_popup_showing;
int peak=-1, pt1=-1,pt2; // for s2n, integrate

GtkWidget* s2n_dialog;
GtkWidget* int_dialog;  
GtkWidget* s2n_label;
GtkWidget* int_label;  
char doing_s2n=0;
char doing_int=0;
GtkWidget *setsf1dialog;

char stored_path[PATH_LENGTH]; // these two are for the save_as command
dbuff *stored_buff=0; // they communicate info from check_overwrite to do_save_wrapper
 


/*need a pivot point on phase change */
/* need to be able to  to bigger and smaller first order phases 
   (ie + and - buttons that increase the scale by 360 deg */




dbuff *create_buff(int num){

  /* here is my menu structure */
    /* menu path,   accelerator   callback, callback action, item_type */
 static  GtkItemFactoryEntry menu_items[]={
    {"/_File",       NULL,         NULL, 0, "<Branch>"},
    {"/File/_New",  "<control>N", file_new,0,NULL},
    {"/File/_Open", "<control>O", file_open,0,NULL},
    {"/File/_Save", "<control>S", file_save,0,NULL},
    {"/File/Save _As", NULL,      file_save_as,0,NULL},
    {"/File/Append", "<control>A", file_append,0,NULL},
    {"/File/_Export",NULL,        file_export,0,NULL},
    {"/File/sep1",  NULL ,        NULL, 0,"<Separator>"},
    {"/File/_Close", NULL,       file_close,0,NULL},
    {"/File/E_xit",   NULL         , file_exit,0,NULL},
    {"/_Display",    NULL,        NULL,0,"<Branch>"},
    {"/Display/_Real", "<alt>R", toggle_disp,0,NULL},
    {"/Display/_Imaginary", "<alt>I", toggle_disp,1,NULL},
    {"/Display/_Magnitude", "<alt>M", toggle_disp,2,NULL},
    {"/Display/_Baseline", "<alt>J", toggle_disp,3,NULL},
    {"/Display/_Store scales",NULL,  do_scales,0,NULL},
    {"/Display/_Apply scales",NULL,  do_scales,1,NULL},
    {"/Display/_User Defined scales",NULL, do_scales,2,NULL},
    {"/_Analysis",   NULL,       NULL,0,"<Branch>"},
    {"/_Analysis/_Phase", "<alt>H", open_phase,0,NULL},
    {"/_Analysis/_Signal to Noise", NULL, signal2noise,0,NULL},
    {"/_Analysis/Signal to Noise old",NULL, signal2noiseold,0,NULL},
    {"/_Analysis/_Integrate", NULL, integrate,0,NULL},
    {"/_Analysis/_Integrate old", NULL, integrateold,0,NULL},
    {"/_Analysis/Integrate from file", NULL, integrate_from_file,0,NULL},
    {"/_Analysis/_Clone from acq buff",NULL,clone_from_acq,0,NULL},
    {"/_Analysis/_Set sf1", NULL, set_sf1,0, NULL},
    {"/_Analysis/R_MS", NULL, calc_rms,0, NULL},
    {"/_Analysis/_Add & subtract",NULL,add_subtract,0,NULL},
    {"/_Baseline", NULL, NULL,0,"<Branch>"},
    {"/_Baseline/_Pick Spline Points", NULL, baseline_spline,PICK_SPLINE_POINTS, NULL},
    {"/_Baseline/_Show Spline Fit", NULL,baseline_spline,SHOW_SPLINE_FIT,NULL},
    {"/_Baseline/_Do Spline", NULL, baseline_spline,DO_SPLINE, NULL},
    {"/_Baseline/_Undo Spline", NULL, baseline_spline,UNDO_SPLINE, NULL},
    {"/_Baseline/_Clear Spline Points", NULL, baseline_spline,CLEAR_SPLINE_POINTS, NULL},
    {"/Hardware/Reset DSP and Synth",NULL, reset_dsp_and_synth,0,NULL}
  };
  GtkWidget *window;
  GtkWidget *menubar;
  GtkWidget *main_vbox,*vbox1,*hbox1,*hbox2,*hbox3,*hbox4,*hbox,*hbox5,*vbox2;
  GtkWidget *canvas,*button;
  GtkWidget *expandb,*expandfb,*fullb,*Bbutton,*bbutton,*Sbutton,*sbutton;
  GtkWidget *autob,*autocheck,*offsetb,*arrow;
  GtkItemFactory *item_factory;
  GtkAccelGroup *accel_group;
  gint nmenu_items = sizeof(menu_items) / sizeof (menu_items[0]);
  char my_string[PATH_LENGTH];
  int i,j,k,inum;
  static int count=0;
  dbuff *buff;
  char old_update_open;
  char s[PATH_LENGTH];
  int save_npts;

  if(num_buffs<MAX_BUFFERS){
    num_buffs++;
    /* set up defaults for buffer */
    buff=g_malloc(sizeof(dbuff));
    if (buff == NULL) perror("buff g_malloc:");
    //    printf("buff create: malloc for buff, buff is: %i bytes\n",sizeof(dbuff));
    buff->scales_dialog = NULL;
    buff->overrun1 = 85*(1+256+65536+16777216);
    buff->overrun2 = 85*(1+256+65536+16777216);
    buff->param_set.npts=0;  //set later
    buff->npts2=1;                  // always
    buff->data=NULL;                // set later
    buff->process_data[PH].val=GLOBAL_PHASE_FLAG;
    buff->flags = 0; // that's only the ft flag at this point
    buff->disp.xx1=0.;
    buff->disp.xx2=1.;
    buff->disp.yy1=0.;
    buff->disp.yy2=1.;
    buff->disp.yscale=1.0;
    buff->disp.yoffset=.0;
    buff->disp.real=1;
    buff->disp.imag=1;
    buff->disp.mag=0;
    buff->disp.base=1;
    buff->disp.dispstyle=SLICE_ROW;
    //buff->disp.dispstyle=RASTER;
    buff->disp.record=0;
    buff->disp.record2=0;
    buff->is_hyper=0;
    buff->win.sizex=800;
    buff->win.sizey=300;
    buff->win.pixmap=NULL;
    buff->win.press_pend=0;
    buff->phase0=0.;
    buff->phase1=0.;
    buff->phase20=0.;
    buff->phase21=0.;
    buff->phase0_app=0.;
    buff->phase1_app=0.;
    buff->phase20_app=0.;
    buff->phase21_app=0.;
    buff->param_set.dwell = 2.0;
    buff->param_set.sw = 1.0/buff->param_set.dwell*1e6;
    buff->buffnum = num;
    buff_resize( buff, 2048, 1 );
    buff->acq_npts=buff->param_set.npts;
    buff->ct = 0;
    path_strcpy ( buff->path_for_reload,"");

    // create buttons now so they exist before call to clone_from_acq
    // they get toggled signals to prevent user from changing during acq, but
    // not hooked up till after we set them initially.
    buff->win.but1a= gtk_radio_button_new_with_label(NULL,"1A");
    buff->win.grp1 = gtk_radio_button_get_group(GTK_RADIO_BUTTON(buff->win.but1a));
    buff->win.but1b= gtk_radio_button_new_with_label(buff->win.grp1,"1B");
    buff->win.grp1 = gtk_radio_button_get_group(GTK_RADIO_BUTTON(buff->win.but1b));
    buff->win.but1c= gtk_radio_button_new_with_label(buff->win.grp1,"1C");
    //    buff->win.grp1 = gtk_radio_button_get_group(GTK_RADIO_BUTTON(buff->win.but1c));

    buff->win.but2a= gtk_radio_button_new_with_label(NULL,"2A");
    buff->win.grp2 = gtk_radio_button_get_group(GTK_RADIO_BUTTON(buff->win.but2a));
    buff->win.but2b= gtk_radio_button_new_with_label(buff->win.grp2,"2B");
    buff->win.grp2 = gtk_radio_button_get_group(GTK_RADIO_BUTTON(buff->win.but2b));
    buff->win.but2c= gtk_radio_button_new_with_label(buff->win.grp2,"2C");
    //    buff->win.grp2 = gtk_radio_button_get_group(GTK_RADIO_BUTTON(buff->win.but2c));




    // if there is already a current buffer, clone its process set 

    if (num_buffs >1 ){
      for(i=0;i<MAX_PROCESS_FUNCTIONS;i++){
	buff->process_data[i].val = buffp[current]->process_data[i].val;
	buff->process_data[i].status = buffp[current]->process_data[i].status;
      }
      buff->phase0 = buffp[current]->phase0;
      buff->phase1 = buffp[current]->phase1;
      buff->phase20 = buffp[current]->phase20;
      buff->phase21 = buffp[current]->phase21;

      buff->param_set.dwell = buffp[current]->param_set.dwell;
      buff->param_set.sw = buffp[current]->param_set.sw;

      buff->param_set.num_acqs=buffp[current]->param_set.num_acqs;
      buff->param_set.num_acqs_2d=buffp[current]->param_set.num_acqs_2d;

    }
    else{ // set some defaults
      buff->param_set.num_acqs=4;
      buff->param_set.num_acqs_2d=1;
      for( i=0; i<MAX_PROCESS_FUNCTIONS; i++ ) {
	buff->process_data[i].val = 0;
	buff->process_data[i].status = PROCESS_OFF;
      }
    buff->process_data[BC1].status = PROCESS_ON;
    buff->process_data[FT].status = PROCESS_ON;
    buff->process_data[BC2].status = PROCESS_ON;
    buff->process_data[PH].status = PROCESS_ON;
    buff->process_data[PH].val = GLOBAL_PHASE_FLAG;
    buff->process_data[ZF].val=2; //this is the default zero fill value
    buff->process_data[CR].val=9; //this is the default cross correlation bits
    }

    path_strcpy(s,getenv("HOME"));
    path_strcat(s,"/Xnmr/data/");
    put_name_in_buff(buff,s);
    //    printf("special buff creation put %s in save\n",buff->param_set.save_path);

    if( acq_in_progress != ACQ_STOPPED && num_buffs == 1 ) { //this is a currently running acq
      buff->win.ct_label = NULL; // clone_from_acq calls upload which tries to write this...
      current_param_set = &buff->param_set; // this used to be after clone_from_acq....
      clone_from_acq(buff,1,NULL);
      do_auto(buff); // do this first time so it looks reasonable

      acq_param_set = &buff->param_set;

      // now, if there is a signal waiting that isn't just data ready, deal with it.
      printf("started up running, signal from acq is: %i\n",data_shm->acq_sig_ui_meaning);
      if ( data_shm->acq_sig_ui_meaning != NEW_DATA_READY){
	printf("there appears to be a signal waiting from acq\n");
	acq_signal_handler();
      }

      //printf( "doing special buffer creation\n" );
      /* this is if we're the only buffer and acq is underway */

    } //end if already in progress

    else { // this is not a currently running acquisition
      buff->param_set.num_parameters = 0;
      if( num_buffs == 1 ) {
	path_strcpy( buff->param_set.exec_path, "");
      }
      else {  //copy old buffer's parameter set if there is another buff
	path_strcpy( buff->param_set.exec_path, buffp[ current ]->param_set.exec_path);
	path_strcpy( my_string, buff->param_set.exec_path);

	//	load_param_file( my_string, &buff->param_set );  // does this need to be there - caused crashes?

	save_npts = buff->param_set.npts;
	buff->acq_npts=buffp[current]->acq_npts;


	memcpy(&buff->param_set,&buffp[current]->param_set,sizeof(parameter_set_t));

	buff->param_set.npts = save_npts;



	for(i=0;i<buff->param_set.num_parameters;i++){
	  // must also do 2d
	  if(buff->param_set.parameter[i].type == 'F'){
	    buff->param_set.parameter[i].f_val_2d=g_malloc(buff->param_set.parameter[i].size *sizeof(double));
	    //	    printf("buff create, malloc for 2d\n");
	    for(k=0;k<buff->param_set.parameter[i].size;k++) 
	      buff->param_set.parameter[i].f_val_2d[k]=buffp[current]->param_set.parameter[i].f_val_2d[k];
	  }
	  if(buff->param_set.parameter[i].type == 'I'){
	    buff->param_set.parameter[i].i_val_2d=g_malloc(buff->param_set.parameter[i].size *sizeof(gint));
	    //	    printf("buff create, malloc for 2d\n");
	    for(k=0;k<buff->param_set.parameter[i].size;k++) 
	      buff->param_set.parameter[i].i_val_2d[k]=buffp[current]->param_set.parameter[i].i_val_2d[k];
	  }
	}

	// finished looking for 2d

      }
    }

    // need to do a little trick to prevent errors:
    // this has to do with the box that pops up to say you can't update parameters during acquisition.
    old_update_open = no_update_open;
    no_update_open = 1;

    show_parameter_frame( &buff->param_set );
    no_update_open = old_update_open;

    show_process_frame( buff->process_data );

    count++;

    // put some data into the buffer.

    for(j=0;j<buff->npts2;j++)
      for(i=0;i<buff->param_set.npts;i++){ // should initialize to 0 
	buff->data[2*i+2*buff->param_set.npts*j]
	  =cos(0.02*i*count)*exp(-i/200.)/(j+1);
	buff->data[2*i+1+2*buff->param_set.npts*j]
	  =sin(0.02*i*count)*exp(-i/200.)/(j+1);

      }

    /*    for(j=0;j<buff->npts2;j++)
      for(i=0;i<buff->param_set.npts;i++){ 
	buff->data[2*i+2*buff->param_set.npts*j]
	  =fabs(cos(0.061*i)*exp(-i/200.)+ cos(0.17*i))*cos(0.23*i);
	buff->data[2*i+1+2*buff->param_set.npts*j]
	  =fabs(cos(0.061*i)*exp(-i/200.) + cos(0.17*i))*sin(0.23*i);

	  } */

  
    // temporarily stick in simulated "noisy" data
    /*          {
      int i,j;
      char *mreg;
      float *c,*s,*e;

      mreg=g_malloc(sizeof (char) * buff->param_set.npts);
      c=g_malloc(sizeof (float) * buff->param_set.npts);
      s=g_malloc(sizeof (float) * buff->param_set.npts);
      e=g_malloc(sizeof (float) * buff->param_set.npts);
      
      
      mreg[0] = psrb(10,1);
      for(i=1;i<buff->param_set.npts;i++){
	mreg[i] = psrb(10,0);
	//	printf("%i "  ,mreg[i]);
      }

      for (i=0;i<buff->param_set.npts;i++){
	c[i] = cos(0.04*i);
	s[i] = sin(0.04*i);
	e[i]= exp(-i/75.);
      }

      for(i=0;i<buff->param_set.npts;i++){ // we need data twice through.
	buff->data[2*i] = 0.;
	buff->data[2*i+1]=0.;
	for (j=0;j<=i;j++){ // calculate the data based on all previous pulses
	  buff->data[2*i] += mreg[j]*c[i-j]*e[i-j];
	  buff->data[2*i+1] += mreg[j]*s[i-j]*e[i-j];
	}
	
      }
      g_free(mreg);
      g_free(c);
      g_free(s);
      g_free(e);

      }
    // end noisy 
    */




    /* the window: */
    window=gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_default_size(GTK_WINDOW(window),850,400);
    gtk_window_set_gravity(GTK_WINDOW(window),GDK_GRAVITY_NORTH_WEST);
    gtk_window_move(GTK_WINDOW(window),(count-1)*50,0);


    buff->win.window=window;
    g_signal_connect (G_OBJECT(window),"delete_event", //was "destroy"
		       G_CALLBACK(destroy_buff),NULL);

    set_window_title(buff);

    main_vbox=gtk_vbox_new(FALSE,1);
    //    gtk_container_border_width(GTK_CONTAINER (main_vbox),1);
    gtk_container_add(GTK_CONTAINER (window), main_vbox);
    
    
    accel_group=gtk_accel_group_new();
    item_factory = gtk_item_factory_new(GTK_TYPE_MENU_BAR,"<main>",
					accel_group);
    gtk_item_factory_create_items(item_factory,nmenu_items,menu_items,buff);
    gtk_window_add_accel_group(GTK_WINDOW(window),accel_group);
    menubar=gtk_item_factory_get_widget(item_factory,"<main>");
  
    gtk_box_pack_start (GTK_BOX (main_vbox),menubar,FALSE,TRUE,0);
    gtk_widget_show(menubar);
    
    /* menus done, now do canvas */

    canvas=gtk_drawing_area_new();
    buff->win.canvas=canvas;
    /*      gtk_drawing_area_size(GTK_DRAWING_AREA (canvas),buff->win.sizex,
	    buff->win.sizey); */
    
    hbox1=gtk_hbox_new(FALSE,1);
    
    gtk_box_pack_start(GTK_BOX(hbox1),canvas,TRUE,TRUE,0);
    gtk_widget_show(canvas);
    g_signal_connect(G_OBJECT(canvas),"expose_event",
		       G_CALLBACK (expose_event),buff);
    // configure event was connected in here

    g_signal_connect(G_OBJECT(canvas),"configure_event",
		       G_CALLBACK (configure_event),buff);


    gtk_widget_set_events(canvas,GDK_EXPOSURE_MASK
			  |GDK_BUTTON_PRESS_MASK|GDK_BUTTON_RELEASE_MASK);
    g_signal_connect(G_OBJECT(canvas),"button_press_event",
		       G_CALLBACK (press_in_win_event),buff);
    g_signal_connect(G_OBJECT(canvas),"button_release_event",
		       G_CALLBACK (press_in_win_event),buff);
    vbox1=gtk_vbox_new(FALSE,1);
    
    expandb=gtk_toggle_button_new_with_label("Expand");
    gtk_box_pack_start(GTK_BOX(vbox1),expandb,FALSE,FALSE,0);
    g_signal_connect(G_OBJECT(expandb),"clicked",
		       G_CALLBACK(expand_routine),buff);
    gtk_widget_show(expandb);
    
    expandfb=gtk_toggle_button_new_with_label("Exp First");
    gtk_box_pack_start(GTK_BOX(vbox1),expandfb,FALSE,FALSE,0);
    g_signal_connect(G_OBJECT(expandfb),"clicked",
		       G_CALLBACK(expandf_routine),buff);
    gtk_widget_show(expandfb);
    
    fullb=gtk_button_new_with_label("Full");
    gtk_box_pack_start(GTK_BOX(vbox1),fullb,FALSE,FALSE,0);
    g_signal_connect(G_OBJECT(fullb),"clicked",
		       G_CALLBACK(full_routine),buff);
    gtk_widget_show(fullb);
    
    hbox2=gtk_hbox_new(FALSE,1);
    

    Bbutton = gtk_button_new();
    gtk_widget_set_size_request(Bbutton,0,25);
    arrow = gtk_arrow_new(GTK_ARROW_UP,GTK_SHADOW_OUT);
    gtk_container_add(GTK_CONTAINER(Bbutton) , arrow);

    gtk_widget_show(arrow);
    gtk_widget_show(Bbutton);

    gtk_box_pack_start(GTK_BOX(hbox2),Bbutton,TRUE,TRUE,0);
    g_signal_connect(G_OBJECT(Bbutton),"clicked",
		       G_CALLBACK(Bbutton_routine),buff);
    gtk_widget_show(Bbutton);
    
    Sbutton = gtk_button_new();
    gtk_widget_set_size_request(Sbutton,0,25);
    arrow = gtk_arrow_new(GTK_ARROW_DOWN,GTK_SHADOW_OUT);
    gtk_container_add(GTK_CONTAINER(Sbutton),arrow);
    gtk_widget_show(arrow);
    gtk_box_pack_start(GTK_BOX(hbox2),Sbutton,TRUE,TRUE,0);
    g_signal_connect(G_OBJECT(Sbutton),"clicked",
		       G_CALLBACK(Sbutton_routine),buff);
    gtk_widget_show(Sbutton);
    
    gtk_widget_show(hbox2);
    gtk_box_pack_start(GTK_BOX(vbox1),hbox2,FALSE,FALSE,0);
    
    /* now lowercase buttons */
    hbox3=gtk_hbox_new(FALSE,1);
    
    bbutton=gtk_button_new();
    gtk_widget_set_size_request(bbutton,0,25);
    arrow = gtk_arrow_new(GTK_ARROW_UP,GTK_SHADOW_ETCHED_OUT);
    gtk_container_add(GTK_CONTAINER(bbutton),arrow);
    gtk_widget_show(arrow);
    gtk_box_pack_start(GTK_BOX(hbox3),bbutton,TRUE,TRUE,0);
    g_signal_connect(G_OBJECT(bbutton),"clicked",
		       G_CALLBACK(bbutton_routine),buff);
    gtk_widget_show(bbutton);
    
    sbutton=gtk_button_new();
    gtk_widget_set_size_request(sbutton,0,25);
    arrow = gtk_arrow_new(GTK_ARROW_DOWN,GTK_SHADOW_ETCHED_IN);
    gtk_container_add(GTK_CONTAINER(sbutton),arrow);
    gtk_widget_show(arrow);
    gtk_box_pack_start(GTK_BOX(hbox3),sbutton,TRUE,TRUE,0);
    g_signal_connect(G_OBJECT(sbutton),"clicked",
		       G_CALLBACK(sbutton_routine),buff);
    gtk_widget_show(sbutton);
    
    gtk_widget_show(hbox3);
    gtk_box_pack_start(GTK_BOX(vbox1),hbox3,FALSE,FALSE,0);
    

    hbox4=gtk_hbox_new(FALSE,1);
    gtk_box_pack_start(GTK_BOX(vbox1),hbox4,FALSE,FALSE,0);
    autocheck=gtk_check_button_new();
    buff->win.autocheck=autocheck;

    // set to auto
    if (num_buffs == 1)
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(buff->win.autocheck),FALSE);
    else   if (GTK_TOGGLE_BUTTON(buffp[current]->win.autocheck)->active)
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(buff->win.autocheck),TRUE);

    g_signal_connect(G_OBJECT(autocheck),"clicked",
		      G_CALLBACK(autocheck_routine),buff);
    gtk_widget_show(autocheck);
    gtk_box_pack_start(GTK_BOX(hbox4),autocheck,FALSE,FALSE,1);
    gtk_widget_show(hbox4);
   


    autob=gtk_button_new_with_label("Auto");
    gtk_box_pack_start(GTK_BOX(hbox4),autob,TRUE,TRUE,0);
    g_signal_connect(G_OBJECT(autob),"clicked",
		       G_CALLBACK(auto_routine),buff);

    gtk_widget_show(autob);
    



    offsetb=gtk_toggle_button_new_with_label("Offset");
    gtk_box_pack_start(GTK_BOX(vbox1),offsetb,FALSE,FALSE,0);
    g_signal_connect(G_OBJECT(offsetb),"clicked",
		       G_CALLBACK(offset_routine),buff);
    gtk_widget_show(offsetb);
    
    buff->win.hypercheck=gtk_check_button_new_with_label("Is Hyper");
    gtk_box_pack_start(GTK_BOX(vbox1),buff->win.hypercheck,FALSE,FALSE,0);
    gtk_widget_show(buff->win.hypercheck);
    g_signal_connect(G_OBJECT(buff->win.hypercheck),"clicked",
		       G_CALLBACK(hyper_check_routine),buff);
    
    
    hbox=gtk_hbox_new(FALSE,1);
    gtk_box_pack_start(GTK_BOX(vbox1),hbox,FALSE,FALSE,0);
    gtk_widget_show(hbox);

    button=gtk_button_new_with_label("+");
    gtk_box_pack_start(GTK_BOX(hbox),button,TRUE,TRUE,0);
    g_signal_connect(G_OBJECT(button),"clicked",
		       G_CALLBACK(plus_button),buff);
    gtk_widget_show(button);

    button=gtk_button_new_with_label("-");
    gtk_box_pack_start(GTK_BOX(hbox),button,TRUE,TRUE,0);
    g_signal_connect(G_OBJECT(button),"clicked",
		       G_CALLBACK(minus_button),buff);
    gtk_widget_show(button);
    gtk_widget_show(hbox);

    buff->win.row_col_lab=gtk_label_new("Row");
    gtk_widget_show(buff->win.row_col_lab);
    button=gtk_button_new();
    gtk_widget_show(button);
    g_signal_connect(G_OBJECT(button),"clicked",
		       G_CALLBACK(row_col_routine),buff);
    gtk_container_add(GTK_CONTAINER(button),buff->win.row_col_lab);
    gtk_box_pack_start(GTK_BOX(vbox1),button,FALSE,FALSE,0);


    buff->win.slice_2d_lab=gtk_label_new("2D");
    gtk_widget_show(buff->win.slice_2d_lab);
    button=gtk_button_new();
    gtk_widget_show(button);
    g_signal_connect(G_OBJECT(button),"clicked",
		       G_CALLBACK(slice_2D_routine),buff);
    gtk_container_add(GTK_CONTAINER(button),buff->win.slice_2d_lab);
    gtk_box_pack_start(GTK_BOX(vbox1),button,FALSE,FALSE,0);

    buff->win.p1_label = gtk_label_new("p1: 0");
    gtk_box_pack_start(GTK_BOX(vbox1),buff->win.p1_label,FALSE,FALSE,0);
    gtk_widget_show(buff->win.p1_label);

    buff->win.p2_label = gtk_label_new("p2: 0");
    gtk_box_pack_start(GTK_BOX(vbox1),buff->win.p2_label,FALSE,FALSE,0);
    gtk_widget_show(buff->win.p2_label);

    buff->win.ct_label = gtk_label_new("ct: 0");
    gtk_box_pack_start(GTK_BOX(vbox1),buff->win.ct_label,FALSE,FALSE,0);
    gtk_widget_show(buff->win.ct_label);


    hbox5=gtk_hbox_new(FALSE,1);
    vbox2=gtk_vbox_new(FALSE,1);

    // move creation of channel buttons to above call to clone_from_acq 
    // so that the buttons exist before we try to set them

    gtk_box_pack_start(GTK_BOX(vbox2),buff->win.but1a,FALSE,FALSE,0);
    gtk_box_pack_start(GTK_BOX(vbox2),buff->win.but1b,FALSE,FALSE,0);
    gtk_box_pack_start(GTK_BOX(vbox2),buff->win.but1c,FALSE,FALSE,0);
    gtk_widget_show(buff->win.but1a);
    gtk_widget_show(buff->win.but1b);
    gtk_widget_show(buff->win.but1c);
    gtk_box_pack_start(GTK_BOX(hbox5),vbox2,FALSE,FALSE,0);
    gtk_widget_show(vbox2);
    

    vbox2=gtk_vbox_new(FALSE,1);

    
    gtk_box_pack_start(GTK_BOX(vbox2),buff->win.but2a,FALSE,FALSE,0);
    gtk_box_pack_start(GTK_BOX(vbox2),buff->win.but2b,FALSE,FALSE,0);
    gtk_box_pack_start(GTK_BOX(vbox2),buff->win.but2c,FALSE,FALSE,0);
    gtk_widget_show(buff->win.but2a);
    gtk_widget_show(buff->win.but2b);
    gtk_widget_show(buff->win.but2c);
    gtk_box_pack_start(GTK_BOX(hbox5),vbox2,FALSE,FALSE,0);
    gtk_widget_show(vbox2);

    gtk_box_pack_start(GTK_BOX(vbox1),hbox5,FALSE,FALSE,0);
    gtk_widget_show(hbox5);
    
    // want to set up in here...
    if (num_buffs >1){
      set_ch1(buff,get_ch1(buffp[current]));
      set_ch2(buff,get_ch2(buffp[current]));
    }
    else {
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(buff->win.but1a),TRUE);
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(buff->win.but2c),TRUE);
    }

    g_signal_connect(G_OBJECT( buff->win.but1a ), "toggled", G_CALLBACK( channel_button_change ),  (void*) buff);
    g_signal_connect(G_OBJECT( buff->win.but1b ), "toggled", G_CALLBACK( channel_button_change ),  (void*) buff);
    g_signal_connect(G_OBJECT( buff->win.but1c ), "toggled", G_CALLBACK( channel_button_change ),  (void*) buff);
    g_signal_connect(G_OBJECT( buff->win.but2a ), "toggled", G_CALLBACK( channel_button_change ),  (void*) buff);
    g_signal_connect(G_OBJECT( buff->win.but2b ), "toggled", G_CALLBACK( channel_button_change ),  (void*) buff);
    g_signal_connect(G_OBJECT( buff->win.but2c ), "toggled", G_CALLBACK( channel_button_change ),  (void*) buff);


    gtk_widget_show(vbox1);
    gtk_box_pack_start(GTK_BOX(hbox1),vbox1,FALSE,FALSE,0);
    gtk_widget_show(hbox1);
    gtk_box_pack_start(GTK_BOX(main_vbox),hbox1,TRUE,TRUE,0);
    
    gtk_widget_show (main_vbox);


    gtk_widget_show(window);

    //    printf("returning from create_buff\n");

    
    // deal with the add_subtract buffer lists:

// deal with add_sub buffer numbers
    sprintf(s,"%i",num);
    inum = num_buffs-1;
    for (i=0;i<num_buffs;i++)
      if (add_sub.index[i] >  num){ // got it - put it here
	//	printf("got buffer for insert at: index: %i\n",i);
	inum = i;
	i = num_buffs;
      }
    
    for (j=num_buffs;j>inum;j--)
	add_sub.index[j] = add_sub.index[j-1];
    gtk_combo_box_insert_text(GTK_COMBO_BOX(add_sub.s_buff1),inum,s);
    gtk_combo_box_insert_text(GTK_COMBO_BOX(add_sub.s_buff2),inum,s);
    gtk_combo_box_insert_text(GTK_COMBO_BOX(add_sub.dest_buff),inum+1,s);
    add_sub.index[inum] = num;
    

    return buff;
    
  }
  else return NULL;
}




gint expose_event(GtkWidget *widget,GdkEventExpose *event,dbuff *buff)
{
  
gdk_draw_drawable(widget->window,
		widget->style->fg_gc[GTK_WIDGET_STATE (widget)],
		buff->win.pixmap,
		event->area.x, event->area.y,
		event->area.x, event->area.y,
		event->area.width, event->area.height);
return FALSE; 
}


gint configure_event(GtkWidget *widget,GdkEventConfigure *event,
			 dbuff *buff)
{

  int sizex,sizey;
  //printf("in configure\n");
  /* get the size of the drawing area */
  sizex=widget->allocation.width-2;
  sizey=widget->allocation.height-2;
  
  /* release old pixmap if there is one */
  if (buff->win.pixmap)
    gdk_drawable_unref(buff->win.pixmap);
  
  /* get new pixmap */
  buff->win.pixmap=gdk_pixmap_new(widget->window,sizex+2,sizey+2,-1);
  buff->win.sizex=sizex;
  buff->win.sizey=sizey;
  
  /* redraw the canvas */
  draw_canvas(buff);
  show_active_border();
  return TRUE;
}

void draw_canvas(dbuff *buff)
{
 GdkRectangle rect;




 // printf( "drawing canvas\n");


 /* clear the canvas */
 rect.x=1;
 rect.y=1;
 rect.width=buff->win.sizex;
 rect.height=buff->win.sizey;

 // printf("doing draw canvas\n");
 cursor_busy(buff);
 gdk_draw_rectangle(buff->win.pixmap,buff->win.canvas->style->white_gc,TRUE,
		    rect.x,rect.y,rect.width,rect.height);


 if (colourgc == NULL){
   //printf("in draw_canvas, gc is NULL\n");
   return;
 }

 if(buff->disp.dispstyle==SLICE_ROW){
   if (GTK_TOGGLE_BUTTON(buff->win.autocheck)->active)
     do_auto(buff);
   //   else
   draw_oned(buff,0.,0.,&buff->data[buff->disp.record*2*buff->param_set.npts],
	       buff->param_set.npts);


 }
 else if(buff->disp.dispstyle==SLICE_COL){
   if(GTK_TOGGLE_BUTTON(buff->win.autocheck)->active)
     do_auto2(buff);
   draw_oned2(buff,0.,0.);

 }

 else if(buff->disp.dispstyle==RASTER)
   draw_raster(buff);

 
 gtk_widget_queue_draw_area(buff->win.canvas,rect.x,rect.y,rect.width,rect.height);
 cursor_normal(buff);


 
 return;
}


void draw_raster(dbuff *buff)

{

  int i1,i2,j1,j2,ci;
  int j,i,npts1,yp1,yp2,xp1,xp2,npts2;
  float max,min,da;
  

  npts1=buff->param_set.npts;
  npts2=buff->npts2;

  /* first figure out start and end points */
  i1=(int) (buff->disp.xx1 * (npts1-1) +.5);
  i2=(int) (buff->disp.xx2 * (npts1-1) +.5);

  j1=(int) (buff->disp.yy1 * (npts2-1) +.5);
  j2=(int) (buff->disp.yy2 * (npts2-1) +.5);

  //if  is_hyper, both j1 and j2 must be even
  if (buff->is_hyper){
    if (j1 %2 ==1) j1-=1;
    if (j2 %2 ==1) j2-=1;
  }
  if (j1==j2){
    if(j2>0) j1=j2-1;
    else j2=j1+1;
    if (j2 >= buff->npts2/(1+buff->is_hyper)) j2=j2-1; // in case of hyper with only 2 pts
  }

  if (i1==i2) {
    if (i2 >0 ) i1=i2-1;
    else i2=i1+1;
  }

  /* now search through, find max and min to get colour levels */
  max=buff->data[2*i1+j1*npts1*2];
  min=max;

  for (i=i1;i<=i2;i++){
    for(j=j1;j<=j2;j+=(1+buff->is_hyper)){  // only search through the ones we'll display
      da=buff->data[2*i+j*npts1*2];
      if(da>max) max=da;
      if(da<min) min=da;
    }
  }
  /* want to set max and min to same value, but with opposite signs */
  if( fabs(max) > fabs(min)) max=fabs(max);
  else  max=fabs(min);
  min=-max;



  /* now, if we have more points to show than there are pixels in the x
   direction, step through pixels and draw lines

   otherwise, step through data points and draw rectangles */
  /* our colours are in 0 -> NUM_COLOURS-1 */

  if (i2-i1 < buff->win.sizex){  /* here we'll step through data */
    yp1=buff->win.sizey+1;
    for(j=j1;j<=j2;j+=(1+buff->is_hyper)){
      yp2=buff->win.sizey - (j+1+buff->is_hyper-j1)
	*buff->win.sizey/(j2+1+buff->is_hyper-j1)+1;
      xp1= 1;
      for(i=i1;i<=i2;i++){
	xp2=(i+1-i1)*buff->win.sizex/(i2+1-i1)+1;
	da=buff->data[2*i+j*npts1*2];
	//	ci=(int) floor((da-min)/(max-min)*(NUM_COLOURS-1) +.5);
	ci=(int) floor((da-min)/(max-min)*(NUM_COLOURS-1e-12));
	ci=NUM_COLOURS-ci;
	
	gdk_gc_set_foreground(colourgc,&colours[ci-1]);
	/* and draw the rectangle */
	gdk_draw_rectangle(buff->win.pixmap
			   ,colourgc,TRUE,
			   xp1,yp2,xp2-xp1,yp1-yp2);
	xp1=xp2;
	
      }
      yp1=yp2;
    }
  }
  else {  /* now, if there are more points than pixels */
    yp1=buff->win.sizey+1;
    for(j=j1;j<=j2;j+=(1+buff->is_hyper)){
      yp2=buff->win.sizey - (j+1+buff->is_hyper-j1)
	*buff->win.sizey/(j2+1+buff->is_hyper-j1)+1;
      xp1= 1;
      for(i=i1;i<=i2;i++){
	xp2=(i+1-i1)*buff->win.sizex/(i2+1-i1)+1;
	da=buff->data[2*i+j*npts1*2];
	//	ci=(int) floor((da-min)/(max-min)*(NUM_COLOURS-1) +.5);
	ci=(int) floor((da-min)/(max-min)*(NUM_COLOURS-1e-12));
	ci=NUM_COLOURS-ci;
	
	gdk_gc_set_foreground(colourgc,&colours[ci-1]);
	/* and draw the rectangle */
	gdk_draw_rectangle(buff->win.pixmap
			   ,colourgc,TRUE,
			   xp1,yp2,xp2-xp1,yp1-yp2);
	xp1=xp2;
	
      }
      yp1=yp2;
    }
  }
  
  return; 
}



void draw_row_trace(dbuff *buff, float extraxoff,float extrayoff
		    ,float *data,int npts, GdkColor *col,int ri){
  int x,y,i1,i2,x2,y2,exint,eyint,i;
  /* first point */


  // ri is real or imag = 0 or 1


  if (buff->disp.dispstyle == SLICE_ROW) { //normal situation
    i1=(int) (buff->disp.xx1 * (npts-1) +.5);
    i2=(int) (buff->disp.xx2 * (npts-1) +.5);


    if (i1==i2) {
      if (i2 >0 ) 
	i1=i2-1;
      else 
	i2=i1+1;
    }
    
    exint= extraxoff*(buff->win.sizex-1)/(buff->disp.xx2-buff->disp.xx1);
    eyint= extrayoff*buff->disp.yscale/2.;
  }
  else if (buff->disp.dispstyle == SLICE_COL){ // only if we're phasing on a column
    
    i1=(int) (buff->disp.yy1 * (npts-1)+.5);
    i2=(int) (buff->disp.yy2 * (npts-1)+.5);
    
    if (i1==i2) {
      if (i2 >0 ) 
	i1=i2-1;
      else 
	i2=i1+1;
    }
    /*  i can be odd here - can not be odd in draw_oned2
	if (buff->is_hyper){
	if (i1 %2 ==1) i1-=1;
	if (i2 %2 ==1) i2-=1;
	} */
    
    exint= extraxoff*buff->win.sizex/(buff->disp.yy2-buff->disp.yy1);
    eyint= extrayoff*buff->disp.yscale/2.;
    
  }
  else {
    printf("in draw_oned and isn't a row or a column\n");
    return;
  }
 

  x= 1;
  y=(int)  -((data[i1*2+ri]
	      +buff->disp.yoffset)*buff->win.sizey
	     *buff->disp.yscale/2.-buff->win.sizey/2.)+1.5;
  y = MIN(y,buff->win.sizey);
  y = MAX(y,1);
  gdk_gc_set_foreground(colourgc,col);
  

 // new way: much faster!
 dpoints[0].x = x+exint;
 dpoints[0].y = y+eyint;


 for(i=i1+1;i<=i2;i++){
   x2= (i-i1)*(buff->win.sizex-1)/(i2-i1)+1;
   y2=(int) -((data[i*2+ri]
	       +buff->disp.yoffset)*buff->win.sizey*
	      buff->disp.yscale/2.-buff->win.sizey/2.)+1.5;
   y2 = MIN(y2,buff->win.sizey);
   y2 = MAX(y2,1);
   dpoints[i-i1].x = x2+exint;
   dpoints[i-i1].y = y2+eyint;
 }
 gdk_draw_lines(buff->win.pixmap,colourgc,dpoints,
		   i2-i1+1);





}



void draw_oned(dbuff *buff,float extraxoff,float extrayoff,float *data
	       ,int npts)
{
  /* pass it both the data and buffer for phasing purposes */
 
  int i,exint,eyint,y;
  float * temp_data;



  exint= extraxoff*(buff->win.sizex-1)/(buff->disp.xx2-buff->disp.xx1);
  eyint= extrayoff*buff->disp.yscale/2.;

  /* draw real */

  if(buff->disp.real){
    /* first point */
    draw_row_trace(buff,extraxoff,extrayoff,data,npts,&colours[RED],0);

  }
  if(buff->disp.imag){
    /* first point */

    draw_row_trace(buff,extraxoff,extrayoff,data,npts,&colours[GREEN],1);
    
  }
  if(buff->disp.mag){
    temp_data = g_malloc(8*buff->param_set.npts);
    for (i=0;i<buff->param_set.npts;i++)
      temp_data[i*2] = sqrt(data[2*i]*data[2*i]+data[2*i+1]*data[2*i+1]);
    draw_row_trace(buff,extraxoff,extrayoff,temp_data,npts,&colours[BLUE],0);
    g_free(temp_data);

  }

  if(buff->disp.base){
    y=-buff->disp.yoffset*buff->win.sizey*buff->disp.yscale/2.
      +buff->win.sizey/2.+1.5;
    gdk_draw_line(buff->win.pixmap,buff->win.canvas->style->black_gc,
		  1+exint,eyint+y,buff->win.sizex+exint,y);

  }


return;
}


void draw_oned2(dbuff *buff,float extraxoff,float extrayoff)
{

  // pass it in the data for phasing purposes

  int i,i1,i2,x,x2,y,y2,exint,eyint,recadd;
  float *data;
  int ndpoints;

  data=buff->data;


  if (buff->is_hyper == 0){

    i1=(int) (buff->disp.yy1 * (buff->npts2-1)+.5);
    i2=(int) (buff->disp.yy2 * (buff->npts2-1)+.5);
  }
  else{
    i1 = (int) (buff->disp.yy1 * (buff->npts2/2-1)+.5);
    i2 = (int) (buff->disp.yy2 * (buff->npts2/2-1)+.5);
    i1 *= 2;
    i2 *= 2;
  }


  if (i1==i2) {
    if (i2 >0 ) 
      i1=i2-1;
    else 
      i2=i1+1;
  }



  exint= extraxoff*buff->win.sizex/(buff->disp.yy2-buff->disp.yy1);
  eyint= extrayoff*buff->disp.yscale/2.;

  /* draw real */
  recadd=2*buff->param_set.npts;

  if(buff->disp.real){
    /* first point */
    x= 1;
    y=(int) -((data[recadd*i1+2*buff->disp.record2]
	       +buff->disp.yoffset)*buff->win.sizey
	      *buff->disp.yscale/2.-buff->win.sizey/2.)+1.5;
    y = MIN(y,buff->win.sizey);
    y = MAX(y,1);
    gdk_gc_set_foreground(colourgc,&colours[RED]);

    dpoints[0].x = x+exint;
    dpoints[0].y = y+eyint;

    ndpoints = 1;
    for( i = i1+1+buff->is_hyper ; i <= i2 ; i += (1+buff->is_hyper )){
      x2=(i-i1)*(buff->win.sizex-1)/(i2-i1)+1;
      y2=(int) -((data[recadd*i+2*buff->disp.record2]
		  +buff->disp.yoffset)*buff->win.sizey*
	      buff->disp.yscale/2.-buff->win.sizey/2.)+1.5;
      y2 = MIN(y2,buff->win.sizey);
      y2 = MAX(y2,1);
      
      dpoints[ndpoints].x = x2+exint;
      dpoints[ndpoints].y = y2+eyint;
      ndpoints += 1;
    }
    gdk_draw_lines(buff->win.pixmap,colourgc,dpoints,
		  ndpoints);
  }
   if(buff->disp.imag && buff->is_hyper){

    x= 1;
    y=(int) -((data[recadd*(i1+1)+2*buff->disp.record2]
	       +buff->disp.yoffset)*buff->win.sizey
	      *buff->disp.yscale/2.-buff->win.sizey/2.)+1.5;
    y = MIN(y,buff->win.sizey);
    y = MAX(y,1);

    gdk_gc_set_foreground(colourgc,&colours[GREEN]);
    dpoints[0].x = x+exint;
    dpoints[0].y = y+eyint;

    ndpoints = 1;

    for(i=i1+1+buff->is_hyper;i<=i2;i+=(1+buff->is_hyper)){
      x2=(i-i1)*(buff->win.sizex-1)/(i2-i1)+1;
      y2=(int) -((data[recadd*(i+1)+2*buff->disp.record2]
		  +buff->disp.yoffset)*buff->win.sizey*
	      buff->disp.yscale/2.-buff->win.sizey/2.)+1.5;
      y2 = MIN(y2,buff->win.sizey);
      y2 = MAX(y2,1);

      dpoints[ndpoints].x = x2+exint;
      dpoints[ndpoints].y = y2+eyint;
      ndpoints += 1;
    }
    gdk_draw_lines(buff->win.pixmap,colourgc,dpoints
		    ,ndpoints);
   } 
   if(buff->disp.mag && buff->is_hyper){
    x= 1;
    y=(int) -((sqrt((data[recadd*(i1+1)+2*buff->disp.record2])
		    *(data[recadd*(i1+1)+2*buff->disp.record2])
		    +(data[recadd*i1+2*buff->disp.record2])
		    *(data[recadd*i1+2*buff->disp.record2]))
	       +buff->disp.yoffset)*buff->win.sizey
	      *buff->disp.yscale/2.-buff->win.sizey/2.)+1.5;
    y = MIN(y,buff->win.sizey);
    y = MAX(y,1);
    
    gdk_gc_set_foreground(colourgc,&colours[BLUE]);
    dpoints[0].x = x+exint;
    dpoints[0].y = y+eyint;

    ndpoints = 1;
    
    for(i=i1+1+buff->is_hyper;i<=i2;i+=(1+buff->is_hyper)){
      x2=(i-i1)*(buff->win.sizex-1)/(i2-i1)+1;
      y2=(int) -((sqrt((data[recadd*(i+1)+2*buff->disp.record2])
		       *(data[recadd*(i+1)+2*buff->disp.record2])
		       +(data[recadd*i+2*buff->disp.record2])
		       *(data[recadd*i+2*buff->disp.record2]))
		  +buff->disp.yoffset)*buff->win.sizey*
	      buff->disp.yscale/2.-buff->win.sizey/2.)+1.5;
      y2 = MIN(y2,buff->win.sizey);
      y2 = MAX(y2,1);
      dpoints[ndpoints].x = x2+exint;
      dpoints[ndpoints].y = y2+eyint;
      ndpoints += 1;
    }
    gdk_draw_lines(buff->win.pixmap,colourgc,dpoints,ndpoints);
   } 

  if(buff->disp.base){
    y=-buff->disp.yoffset*buff->win.sizey*buff->disp.yscale/2.
      +buff->win.sizey/2.+1.5;
    gdk_draw_line(buff->win.pixmap,buff->win.canvas->style->black_gc,
		  1+exint,eyint+y,buff->win.sizex+exint,y);
  }
return;
}



void file_open(dbuff *buff,int action,GtkWidget *widget)

{
  GtkWidget *filew;
  char s[PATH_LENGTH];

  if (buff->buffnum == upload_buff && acq_in_progress != ACQ_STOPPED){
    popup_msg("Can't open while Acquisition is running\n");
    return;
  }
  filew = gtk_file_selection_new ("Load");

  // Connect the ok_button
  g_signal_connect (G_OBJECT( GTK_FILE_SELECTION(filew)->ok_button), "clicked", G_CALLBACK (do_load_wrapper),
		      GTK_FILE_SELECTION (filew)  );
        
  // Connect the cancel_button
  g_signal_connect_swapped (G_OBJECT (GTK_FILE_SELECTION(filew)->cancel_button), "clicked", 
			     G_CALLBACK( gtk_widget_destroy), G_OBJECT (filew));
        
  g_object_set_data( G_OBJECT( filew ), BUFF_KEY, buff );

  
  getcwd(s,PATH_LENGTH);
  path_strcat(s,"/");
  gtk_file_selection_set_filename ( GTK_FILE_SELECTION (filew),s);

  // place the window
  gtk_window_set_transient_for(GTK_WINDOW(filew),GTK_WINDOW(buff->win.window));
  gtk_window_set_position(GTK_WINDOW(filew),GTK_WIN_POS_CENTER_ON_PARENT);
  gtk_window_set_modal(GTK_WINDOW(filew),TRUE);
  gtk_widget_show( filew );
        
  return;
}

gint destroy_buff(GtkWidget *widget,gpointer data)
{
  int bnum=0;
  int i,j;
  dbuff *buff;
  char old_update_open;

  // sadly,  it appears as though gtk is slightly broken, delete event doesn't pass 
  // back the data it should, so need to figure out what buff this is.
 
  for (i=0;i<MAX_BUFFERS;i++){
    if (buffp[i] != NULL)
      if (buffp[i]->win.window == widget){
	//	printf("got delete_event from buff: %i\n",i);
	bnum = i;
	i = MAX_BUFFERS;
      }
  }



  
  buff = buffp[bnum];

  // find reasons to not quit

  if(buff->win.press_pend > 0 && from_do_destroy_all == 0 ){ // if we're killing everything, kill this one too.
    popup_msg("Can't close buffer while press pending (integrate or expand or S2N or set sf1 or phase)");
    return TRUE;
  }


  if (phase_data.buffnum == bnum && phase_data.is_open == 1){
    phase_buttons(GTK_WIDGET(phase_data.cancel),NULL);
  }


  if ( popup_data.bnum == bnum &&  array_popup_showing == 1 ){
    array_cancel_pressed(NULL,NULL); // both args are bogus
    //    popup_msg("Close Array window before closing buffer!");
    //    return TRUE;
  }


  if (  buff->scales_dialog != NULL){
    //    printf("killing scales window for buffer %i\n",bnum);
    do_wrapup_user_scales(GTK_WIDGET(buff->scales_dialog),buff);
      //    popup_msg("close scales dialog before closing buffer");
    //    return TRUE;
  }


  /* if this is the last buffer, clean up and get out, let acq run */

 


  //  printf("upload_buff is: %i, acq_in_progress is : %i\n",upload_buff,acq_in_progress);

  if ( from_do_destroy_all == 0 ){ //means from ui, not from a signal
    //    printf("not from a destroy all\n");

    // ok, if this is the acq buffer and the user selected close or x'd the window:
    if (bnum == upload_buff && acq_in_progress != ACQ_STOPPED){
      printf("Can't close Acquisition Buffer !!\n");
      popup_msg("Can't close Acquisition Buffer!!");
      return TRUE; //not destroyed, but event handled
    }
  }



// deal with add_sub buffer numbers
  for (i=0;i<num_buffs;i++){
    if (add_sub.index[i] == bnum){ // got it
      //      printf("got buffer at index: %i\n",i);
      for (j=i;j<num_buffs;j++)
	add_sub.index[j] = add_sub.index[j+1];
      gtk_combo_box_remove_text(GTK_COMBO_BOX(add_sub.s_buff1),i);
      gtk_combo_box_remove_text(GTK_COMBO_BOX(add_sub.s_buff2),i);
      gtk_combo_box_remove_text(GTK_COMBO_BOX(add_sub.dest_buff),i+1);
    }
  }



  num_buffs -=1;

  if (num_buffs == 0 && no_acq == FALSE)
    release_ipc_stuff(); // this detaches shared mem and sets our pid to -1 in it.

  /* free up all the things we alloc'd */

  g_free(buff->data);
  
// if any 2d params, free them

  clear_param_set_2d(&buff->param_set);

  { int i;
  //  gtk_signal_disconnect_by_data(GTK_OBJECT(buff->win.canvas),buff);
  i=g_signal_handlers_disconnect_matched(G_OBJECT(buff->win.canvas),G_SIGNAL_MATCH_DATA,0,0,NULL,NULL,buff);
  //  printf("disconnected: %i handlers\n",i);
}
  gdk_pixmap_unref(buff->win.pixmap);

  gtk_widget_destroy( GTK_WIDGET(buff->win.window) ); 
  g_free(buff);
  
  buffp[bnum]=NULL;


  




  if (num_buffs == 0){
    gdk_cursor_unref(cursorclock);
    gtk_main_quit();  
    printf("called gtk_main_quit\n");

 }


  /*
   * if this isn't the last buffer, reset the global variables current and last_current  
   * and stop acq
   */

  else {

    if (current == bnum) // search for a new current buffer if ours was
      for( current = 0; buffp[ current ] == NULL; current++ );

    show_active_border();  
    gdk_window_raise( buffp[ current ]->win.window->window );

    old_update_open = no_update_open;
    no_update_open = 1;
    show_parameter_frame( &buffp[ current ]->param_set );
    no_update_open = old_update_open;

    show_process_frame( buffp[ current ]->process_data );

    if (buffp[current]->buffnum== upload_buff && acq_in_progress == ACQ_RUNNING)
      update_2d_buttons();
    else
      update_2d_buttons_from_buff( buffp[current] );

    //disable any lingering idle_draw calls

    redraw = 0;

  }
  
  return FALSE; //meaning go ahead and destroy the window.

}

void file_close(dbuff *buff,int action,GtkWidget *widget)
{
  /* this is where we come when you select file_close from the menu ,
     We also go to destroy_buff if we get destroyed from the wm
  This seems to be roughly the same sequence that we'd get if we emitted a delete_event signal*/

  gpointer data=NULL;
  int result;
  //  printf("file close for buffer: %i\n",buff->buffnum);
  /* kill the window */
  
  result = destroy_buff(GTK_WIDGET(buff->win.window),data);

  return;
}



void file_save(dbuff *buff,int action,GtkWidget *widget)
{

  printf("in file_save, using path: %s\n",buff->param_set.save_path);
  if (buff->param_set.save_path[strlen(buff->param_set.save_path)-1] == '/'){
    popup_msg("Invalid file name");
    return;
  }
  check_overwrite( buff, buff->param_set.save_path); 
  return;
}

void file_save_as(dbuff *buff,int action,GtkWidget *widget)
{
  GtkWidget *filew;
  char path[PATH_LENGTH];

  filew = gtk_file_selection_new ("Save As");

  getcwd(path,PATH_LENGTH);
  path_strcat(path,"/");
  gtk_file_selection_set_filename ( GTK_FILE_SELECTION (filew), path);

  // Connect the ok_button
  g_signal_connect (G_OBJECT( GTK_FILE_SELECTION(filew)->ok_button), "clicked", 
		      G_CALLBACK (check_overwrite_wrapper), GTK_FILE_SELECTION (filew)  );
        
  // Connect the cancel_button
  g_signal_connect_swapped (G_OBJECT (GTK_FILE_SELECTION(filew)->cancel_button), "clicked", 
			     G_CALLBACK( gtk_widget_destroy), G_OBJECT (filew));
        
  g_object_set_data( G_OBJECT( filew ), BUFF_KEY, buff );

  gtk_window_set_transient_for(GTK_WINDOW(filew),GTK_WINDOW(buff->win.window));
  gtk_window_set_position(GTK_WINDOW(filew),GTK_WIN_POS_CENTER_ON_PARENT);
  gtk_window_set_modal(GTK_WINDOW(filew),TRUE);

  gtk_widget_show( filew );
  return;
  //printf("file save as\n");
}

void file_new(dbuff *buff,int action,GtkWidget *widget)
{
  int i;
  if (num_buffs<MAX_BUFFERS){
    for(i=0;i<MAX_BUFFERS;i++)
      if (buffp[i] == NULL){
	//	printf("using buff #%i\n",i);
	buffp[i]=create_buff(i);
	last_current = current; 
	current = i;
	show_active_border(); 
	i=MAX_BUFFERS;
      }


  }
  else{
    printf("Max buffers in use\n");
    popup_msg("Too many buffers already");
  }
}

void file_exit(dbuff *buff,int action,GtkWidget *widget)
{
  //printf("file_exit\n");
  destroy_all(widget,NULL); 
}
gint unauto(dbuff *buff)             //What is this? -SN
{

  // turns off the "auto" toggle button
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(buff->win.autocheck),FALSE);

  return 0;
}

void do_scales(dbuff *buff,int action,GtkWidget *widget)
{
 static float xx1=0,xx2=1,yy1=0,yy2=1,yscale=1,yoffset=0;
 GtkWidget *inputbox;
 GtkAdjustment *spinner_adj1,*spinner_adj2,*spinner_adj3,*spinner_adj4;
 GtkWidget *table;
 GtkWidget *label1;
 GtkWidget *label2;
 GtkWidget *label3;
 GtkWidget *label4;
 GtkWidget *entry1;
 GtkWidget *entry2;
 GtkWidget *entry3;
 GtkWidget *entry4;
 GtkWidget *OK_button;
 GtkWidget *Update;
  
 char temps[PATH_LENGTH];

switch (action)
  {
  case 0: // save the current scales
    xx1=buff->disp.xx1;
    xx2=buff->disp.xx2;
    yy1=buff->disp.yy1;
    yy2=buff->disp.yy2;
    yscale=buff->disp.yscale;
    yoffset=buff->disp.yoffset;
    break;
  case 1:
    if (buff->scales_dialog != NULL){
      popup_msg("Can't apply while scales dialog open");
      return;
    }
    unauto(buff); // turns off auto scaling if it was on.
    buff->disp.xx1=xx1;
    buff->disp.xx2=xx2;
    buff->disp.yy1=yy1;
    buff->disp.yy2=yy2;
    buff->disp.yscale=yscale;
    buff->disp.yoffset=yoffset;
    draw_canvas(buff);
    break;

  case 2:   //User defined scales

    if ( buff->scales_dialog != NULL){  //this buff already has a scale dialog
      popup_msg("scales dialog already open for this buffer\n");
      return ;
    }
  
    unauto(buff); // turns off auto scaling if it was on.

    inputbox = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    OK_button= gtk_button_new_with_label("Exit");
    Update= gtk_button_new_with_label("Update Scales");
    //    printf("%i\n",(int)buff->disp.xx1);

    spinner_adj1 = (GtkAdjustment *) gtk_adjustment_new(buff->disp.xx1, 0.0, 1.0, .02, 0.1, 0.1);
    spinner_adj2 = (GtkAdjustment *) gtk_adjustment_new(buff->disp.xx2, 0.0, 1.0, .02, 0.1, 0.1);
    spinner_adj3 = (GtkAdjustment *) gtk_adjustment_new(buff->disp.yy1, 0.0, 1.0, .02, 0.1, 0.1);
    spinner_adj4 = (GtkAdjustment *) gtk_adjustment_new(buff->disp.yy2, 0.0, 1.0, .02, 0.1, 0.1);

    entry1 = gtk_spin_button_new (spinner_adj1, 10.0, 5);
    entry2 = gtk_spin_button_new (spinner_adj2, 10.0, 5);
    entry3 = gtk_spin_button_new (spinner_adj3, 10.0, 5);
    entry4 = gtk_spin_button_new (spinner_adj4, 10.0, 5);

    label1 = gtk_label_new("f1min:");
    label2 = gtk_label_new("f1max");
    label3 = gtk_label_new("f2min");
    label4 = gtk_label_new("f2max");

    table = gtk_table_new(2, 5, FALSE);   
    gtk_table_attach(GTK_TABLE(table), label1, 0, 1, 0, 1, GTK_EXPAND | GTK_SHRINK | GTK_FILL, GTK_EXPAND | GTK_SHRINK | GTK_FILL, 0, 0);
    gtk_table_attach(GTK_TABLE(table), label2, 0, 1, 1, 2, GTK_EXPAND | GTK_SHRINK | GTK_FILL, GTK_EXPAND | GTK_SHRINK | GTK_FILL, 0, 0);
    gtk_table_attach(GTK_TABLE(table), label3, 0, 1, 2, 3, GTK_EXPAND | GTK_SHRINK | GTK_FILL, GTK_EXPAND | GTK_SHRINK | GTK_FILL, 0, 0);
    gtk_table_attach(GTK_TABLE(table), label4, 0, 1, 3, 4, GTK_EXPAND | GTK_SHRINK | GTK_FILL, GTK_EXPAND | GTK_SHRINK | GTK_FILL, 0, 0);
    gtk_table_attach(GTK_TABLE(table), entry1, 1, 2, 0, 1, GTK_EXPAND | GTK_SHRINK | GTK_FILL, GTK_EXPAND | GTK_SHRINK | GTK_FILL, 0, 0);
    gtk_table_attach(GTK_TABLE(table), entry2, 1, 2, 1, 2, GTK_EXPAND | GTK_SHRINK | GTK_FILL, GTK_EXPAND | GTK_SHRINK | GTK_FILL, 0, 0);
    gtk_table_attach(GTK_TABLE(table), entry3, 1, 2, 2, 3, GTK_EXPAND | GTK_SHRINK | GTK_FILL, GTK_EXPAND | GTK_SHRINK | GTK_FILL, 0, 0);
    gtk_table_attach(GTK_TABLE(table), entry4, 1, 2, 3, 4, GTK_EXPAND | GTK_SHRINK | GTK_FILL, GTK_EXPAND | GTK_SHRINK | GTK_FILL, 0, 0);
    gtk_table_attach(GTK_TABLE(table), OK_button,0, 1, 4, 5, GTK_EXPAND | GTK_SHRINK | GTK_FILL, GTK_EXPAND | GTK_SHRINK | GTK_FILL, 0, 0);
    gtk_table_attach(GTK_TABLE(table), Update,   1, 2, 4, 5, GTK_EXPAND | GTK_SHRINK | GTK_FILL, GTK_EXPAND | GTK_SHRINK | GTK_FILL, 0, 0);

    g_signal_connect(G_OBJECT(OK_button), "clicked", G_CALLBACK (do_wrapup_user_scales),  buff);
    g_signal_connect(G_OBJECT(Update),    "clicked", G_CALLBACK (do_update_scales), buff );
    g_signal_connect(G_OBJECT(inputbox), "destroy", G_CALLBACK (do_wrapup_user_scales),buff ); // delete_event doesn't actually give back the data...



    //  if the user changes the scales with expand or auto or something, our update scales button won't do what we want.
    // should really just store the entry widgets (into the buff struct ick!) and grab values from them
    // when we do_updates_scales
    g_signal_connect(G_OBJECT(entry1),"changed", G_CALLBACK(do_user_scales), &buff->disp.xx1);
    g_signal_connect(G_OBJECT(entry2),"changed", G_CALLBACK(do_user_scales), &buff->disp.xx2);
    g_signal_connect(G_OBJECT(entry3),"changed", G_CALLBACK(do_user_scales), &buff->disp.yy1);
    g_signal_connect(G_OBJECT(entry4),"changed", G_CALLBACK(do_user_scales), &buff->disp.yy2);

    gtk_container_add(GTK_CONTAINER(inputbox), table);
    //    gtk_container_border_width (GTK_CONTAINER (inputbox), 5);
    gtk_window_set_default_size (GTK_WINDOW(inputbox), 300, 100);
    sprintf(temps,"User Defined Scales, buffer: %i",buff->buffnum);
    gtk_window_set_title(GTK_WINDOW (inputbox),temps);

    // place the window
    gtk_window_set_transient_for(GTK_WINDOW(inputbox),GTK_WINDOW(panwindow));
    gtk_window_set_position(GTK_WINDOW(inputbox),GTK_WIN_POS_CENTER_ON_PARENT);

    gtk_widget_show_all(inputbox);
    buff->scales_dialog = inputbox;

    break;
  }
}

gint do_user_scales(GtkWidget *widget, float *range)
{
  //  *range = gtk_spin_button_get_value_as_float( GTK_SPIN_BUTTON(widget) );
  *range = gtk_spin_button_get_value( GTK_SPIN_BUTTON(widget) );
  //  printf("%f",*range);
  return 0;
}

gint do_update_scales(GtkWidget *widget, dbuff *buff)
{
  float temp;
  if (buff->disp.xx1 > buff->disp.xx2) {
    temp = buff->disp.xx2;
    buff->disp.xx2=buff->disp.xx1;
    buff->disp.xx1=temp;
  }
  if (buff->disp.yy1 > buff->disp.yy2) {
    temp = buff->disp.yy2;
    buff->disp.yy2=buff->disp.yy1;
    buff->disp.yy1=temp;
  }
  draw_canvas(buff);
  return 0;
}

gint do_wrapup_user_scales(GtkWidget *widget, dbuff *buff)
{ 
  if (buff->scales_dialog != NULL){
    do_update_scales(NULL,buff);
    gtk_widget_destroy(buff->scales_dialog);
    buff->scales_dialog = NULL;
  }
  return 0;
}  

void toggle_disp(dbuff *buff,int action,GtkWidget *widget)
{
  switch (action)
    {
    case 0 :
      if(buff->disp.real)  buff->disp.real=0;
      else buff->disp.real=1; 
      break;
    case 1 :
      if(buff->disp.imag)  buff->disp.imag=0;
      else buff->disp.imag=1; 
      break;
    case 2:
      if(buff->disp.mag)  buff->disp.mag=0;
      else buff->disp.mag=1; 
      break;
    case 3:
      if(buff->disp.base) buff->disp.base=0;
      else buff->disp.base=1;
    }      
  draw_canvas(buff);
}
 


gint full_routine(GtkWidget *widget,dbuff *buff)
{
  if (buff->scales_dialog != NULL){
    popup_msg("Can't 'full' while scales dialog open");
    return TRUE;
  }

  //printf("full routine\n");
  if(buff->disp.dispstyle==SLICE_ROW || buff->disp.dispstyle==RASTER){
    buff->disp.xx1=0.;
    buff->disp.xx2=1.0; 
  }
  if(buff->disp.dispstyle==SLICE_COL||buff->disp.dispstyle==RASTER){
    buff->disp.yy1=0.;
    buff->disp.yy2=1.0; 
  }
    draw_canvas(buff);
  return TRUE;
}







void signal2noise( dbuff *buff, int action, GtkWidget *widget )
{
  

  if (buff->win.press_pend > 0){
    popup_msg("Can't start signal to noise while press pending");
    return;
  }
  if (doing_s2n == 1) return;

  doing_s2n = 1;
  buff->win.press_pend=3;

  // build instruction dialog
  
  s2n_dialog = gtk_dialog_new(); 
  s2n_label = gtk_label_new("Click on the peak");
  gtk_container_set_border_width( GTK_CONTAINER(s2n_dialog), 5 ); 
  gtk_box_pack_start ( GTK_BOX( (GTK_DIALOG(s2n_dialog)->vbox) ), s2n_label, FALSE, FALSE, 5 ); 


  // place the dialog:
  gtk_window_set_transient_for(GTK_WINDOW(s2n_dialog),GTK_WINDOW(panwindow));
  gtk_window_set_position(GTK_WINDOW(s2n_dialog),GTK_WIN_POS_CENTER_ON_PARENT);


  gtk_widget_show_all (s2n_dialog); 



  // block the ordinary press event
  g_signal_handlers_block_by_func(G_OBJECT(buff->win.canvas),
				     G_CALLBACK (press_in_win_event),
				     buff);

  // connect our event
  g_signal_connect (G_OBJECT (buff->win.canvas), "button_press_event",
                        G_CALLBACK( s2n_press_event), buff);
  
  return;


}

void do_s2n(int peak, int pt1,int pt2, dbuff *buff)
{

char string[UTIL_LEN];
int count=0,i;
float avg=0,avg2=0,s2n;
 
if (pt1 > buff->param_set.npts || pt2 > buff->param_set.npts || peak > buff->param_set.npts) return; 
if (pt1 <0 || pt2 < 0 || peak < 0) return; 

 for (i = MIN(pt1,pt2); i <= MAX(pt1,pt2);i++){
   count +=1;
   avg += buff->data[buff->param_set.npts*2*buff->disp.record+2*i];
   avg2 += buff->data[buff->param_set.npts*2*buff->disp.record+2*i]*
     buff->data[buff->param_set.npts*2*buff->disp.record+i*2];
 }
 avg = avg/count;
 avg2 = avg2/count;
 
 //      printf("npts: %i, record: %i\n",buff->param_set.npts,buff->disp.record);
 //      printf("avg: %f, avg2: %f, count: %i\n",avg,avg2,count);
 
 s2n = buff->data[buff->param_set.npts*2*buff->disp.record+2*peak]/
   sqrt(avg2 - avg*avg);
 
 draw_canvas (buff);
 snprintf(string,UTIL_LEN,"S/N = %g",s2n );
 printf("%s\n",string);
 popup_msg(string);
 

}


void s2n_press_event(GtkWidget *widget, GdkEventButton *event,dbuff *buff)
{

  if (doing_s2n !=1){
    printf("in s2n_press_event, but not doing_2nd!\n");
    doing_s2n =0;
    return;
  }

  switch (buff->win.press_pend)
    {
    case 3:
      peak = pix_to_x(buff,event->x);
      gtk_label_set_text(GTK_LABEL(s2n_label),"Now one edge of noise");

      // now in here we should search around a little for a real peak


      printf("pixel: %i, x: %i\n",(int) event->x,peak);
      draw_vertical(buff,&colours[BLUE],0.,(int) event->x);

      break;
    case 2:
      gtk_label_set_text(GTK_LABEL(s2n_label),"Other edge of noise");
      pt1 = pix_to_x(buff,event->x);

      draw_vertical(buff,&colours[BLUE],0.,(int) event->x);
      break;
    case 1:
      gtk_widget_destroy(s2n_dialog);
      pt2 = pix_to_x(buff, event->x);
      // now in here we need to calculate the s2n and display it



      g_signal_handlers_unblock_by_func(G_OBJECT(buff->win.canvas),
				     G_CALLBACK (press_in_win_event),
				     buff);
  // disconnect our event

      g_signal_handlers_disconnect_by_func (G_OBJECT (buff->win.canvas), 
                        G_CALLBACK( s2n_press_event), buff);
      doing_s2n = 0;
      do_s2n(peak,pt1,pt2,buff);
      // calculate the s2n
      break;
    }
  buff->win.press_pend--;

}

void signal2noiseold( dbuff *buff, int action, GtkWidget *widget )
{
  if (peak == -1) {
    popup_msg("No old s2n values to use");
    return;
  }
  do_s2n(peak,pt1,pt2 ,buff);
}





void integrate( dbuff *buff, int action, GtkWidget *widget )
{
  

  if (buff->win.press_pend > 0){
    popup_msg("Can't start integrate while press pending");
    return;
  }
  if (doing_int == 1) return;

  doing_int = 1;
  buff->win.press_pend=2;

  // build instruction dialog
  
  int_dialog = gtk_dialog_new(); 
  int_label = gtk_label_new("Click on one edge of the integral");
  gtk_container_set_border_width( GTK_CONTAINER(int_dialog), 5 ); 
  gtk_box_pack_start ( GTK_BOX( (GTK_DIALOG(int_dialog)->vbox) ), int_label, FALSE, FALSE, 5 ); 

  // place the dialog:
  gtk_window_set_transient_for(GTK_WINDOW(int_dialog),GTK_WINDOW(panwindow));
  gtk_window_set_position(GTK_WINDOW(int_dialog),GTK_WIN_POS_CENTER_ON_PARENT);

  gtk_widget_show_all (int_dialog); 



  // block the ordinary press event
  g_signal_handlers_block_by_func(G_OBJECT(buff->win.canvas),
				     G_CALLBACK (press_in_win_event),
				     buff);

  // connect our event
  g_signal_connect (G_OBJECT (buff->win.canvas), "button_press_event",
                        G_CALLBACK( integrate_press_event), buff);
  
  return;


}


void do_integrate(int pt1,int pt2,dbuff *buff)
{

 
 char fileN[PATH_LENGTH]; 
 FILE *fstream=NULL;
 char string[UTIL_LEN],arr_name[PARAM_NAME_LEN];
 int count,i,j,arr_num_for_int,arr_type;
 float integral,f1,f2;
 int export = 1;

 arr_num_for_int=0;
 arr_type=3;
 //set up integration export file

 if (pt1 > buff->param_set.npts || pt2 > buff->param_set.npts) return; 
 if (pt1 < 0 || pt2 < 0) return; 
 
 if (strcmp(buff->path_for_reload,"") == 0){
   popup_msg("Can't export integration, no reload path?");
   //    return;
   export = 0;
 }
 
 path_strcpy(fileN,buff->path_for_reload);
 path_strcat(fileN,"int.txt");
 //  printf("using filename: %s\n",fileN);

 if (export == 1) {
   fstream = fopen(fileN,"w");
   if ( fstream == NULL){
     popup_msg("Error opening file for integration  export");
     export = 0;
     //    return;
   }
 }
 
 //search for what we are arrayed over
 
 for (j=0; j<buff->param_set.num_parameters;j++){
   if (buff->param_set.parameter[j].type== 'F'){
     //printf("%s\n",buff->param_set.parameter[j].name);
     arr_num_for_int=j;
     arr_type=1;
   }
   if (buff->param_set.parameter[j].type== 'I'){
     arr_num_for_int=j;
     arr_type=2;
   }
 }
 strncpy(arr_name,buff->param_set.parameter[arr_num_for_int].name,PARAM_NAME_LEN);

 if (export == 1) fprintf(fstream,"%s %s %s","#",arr_name,", integral value\n");
 
 
 if (buff->flags & FT_FLAG){         //frequency domain
   f1 = -( (float) pt1 * buff->param_set.sw/buff->param_set.npts
	   - (float) buff->param_set.sw/2.);
   f2 = -( (float) pt2 * buff->param_set.sw/buff->param_set.npts
	   - (float) buff->param_set.sw/2.);
 } 
 else{                              //time domain
   f1 = (float) pt1 * buff->param_set.dwell;
   f2 = (float) pt2 * buff->param_set.dwell;
 }
 
 //now do integrate
 
 for (j = 0; j<buff->npts2;j+=buff->is_hyper+1){      //do each slice and output all to screen
   count=0;
   integral=0;
   for (i = MIN(pt1,pt2); i <= MAX(pt1,pt2);i++){
     count +=1;
     integral += buff->data[buff->param_set.npts*2*j+2*i];
   }
   
   
   
   // printf("f1: %f\n", f1);
   // printf("f2: %f\n", f2);
   
   // printf("count: %i f1: %f f2: %f integral: %f\n",count, f1, f2, integral);
   // printf("style: %i record: %i record2: %i\n", buff->disp.dispstyle, buff->disp.record, buff->disp.record2);
   
   integral *=  fabs(f1-f2) /  count;
   
   printf("%f\n",integral);  //print to screen
   
   //now print to file
   if (export == 1){
     if (arr_type==1){ 
       fprintf(fstream,"%f %f\n",
	       buff->param_set.parameter[arr_num_for_int].f_val_2d[j],integral);
     }                                                             //if arrayed over float
     if (arr_type==2){
       fprintf(fstream,"%i %f\n",
	       buff->param_set.parameter[arr_num_for_int].i_val_2d[j],integral);
     }                                                           //if arrayed over integer
   }  
   if (j==buff->disp.record){
     snprintf(string,UTIL_LEN,"Integral = %g",integral);
     popup_msg(string);
   }
   
 }
 if (export == 1) fclose(fstream);
 printf("\n");
 draw_canvas (buff);
}




void integrate_press_event(GtkWidget *widget, GdkEventButton *event,dbuff *buff)
{


  if (doing_int !=1){
    printf("in integrate_press_event, but not doing_2nd!\n");
    doing_int =0;
    return;
  }

  switch (buff->win.press_pend)
    {
    case 2:
      gtk_label_set_text(GTK_LABEL(int_label),"Now the other edge");
      pt1 = pix_to_x(buff,event->x);
      printf("pt1: %i\n", pt1);

      draw_vertical(buff,&colours[BLUE],0.,(int) event->x);
      break;
    case 1:
      gtk_widget_destroy(int_dialog);
      pt2 = pix_to_x(buff, event->x);
      printf("pt2: %i\n", pt2);
      // now in here we need to calculate the integral and display it


      // disconnect our event

      g_signal_handlers_disconnect_by_func (G_OBJECT (buff->win.canvas), 
                        G_CALLBACK( integrate_press_event), buff);
      //      printf("about to unblock old\n");
      g_signal_handlers_unblock_by_func(G_OBJECT(buff->win.canvas),
				     G_CALLBACK (press_in_win_event),
				     buff);

      //      printf("about to do_integrate\n");
      doing_int = 0;
      do_integrate(pt1,pt2,buff);
      // calculate the integral
      break;
    }
  buff->win.press_pend--;

}


void integrateold( dbuff *buff, int action, GtkWidget *widget )
{

  if (pt1 == -1) {
    popup_msg("No old bounds to use");
    return;
  }
  do_integrate(pt1,pt2,buff);
}


void integrate_from_file( dbuff *buff, int action, GtkWidget *widget )
{
   int i_pt1,i_pt2;
   FILE *f_int;
   char filename[PATH_LENGTH];

   path_strcpy(filename,getenv("HOME"));
   path_strcat(filename,"/Xnmr/prog/integrate_from_file_parameters.txt");
   //   f_int = fopen("/usr/people/nmruser/Xnmr/prog/integrate_from_file_parameters.txt","r");
   f_int = fopen(filename,"r");
   if ( f_int == NULL){
    popup_msg("Error opening file for integration");
    return;
   }

   fscanf(f_int,"%i",&i_pt1);
 
   fscanf(f_int,"%i",&i_pt2);
   fclose(f_int);

   do_integrate(i_pt1,i_pt2,buff);
}




gint expand_routine(GtkWidget *widget,dbuff *buff)
{
  //  printf("expand routine\n");
  static char norecur = 0;
  if (norecur == 1){
    norecur = 0;
    return TRUE;
  }

  if (buff->scales_dialog != NULL){
    popup_msg("Can't expand while scales dialog open");
    norecur = 1;
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget),FALSE);
    norecur = 0;
    return TRUE;
  }

  if (buff->win.press_pend==0 &&GTK_TOGGLE_BUTTON(widget)->active
      && (buff->disp.dispstyle==SLICE_ROW ||buff->disp.dispstyle==SLICE_COL
	  ||buff->disp.dispstyle == RASTER)){
    /* do the expand */
    buff->win.toggleb = widget;
    g_signal_handlers_block_by_func(G_OBJECT(buff->win.canvas),
				     G_CALLBACK (press_in_win_event),
				     buff);
    g_signal_connect (G_OBJECT (buff->win.canvas), "button_press_event",
                        G_CALLBACK( expand_press_event), buff);
    buff->win.press_pend=2;
  }
  else if( !GTK_TOGGLE_BUTTON(widget)->active && buff->win.toggleb == widget){
    /* give up on expand */
    g_signal_handlers_disconnect_by_func(G_OBJECT (buff->win.canvas),
                                  G_CALLBACK( expand_press_event),buff);
    buff->win.press_pend=0;
    g_signal_handlers_unblock_by_func(G_OBJECT(buff->win.canvas),
				     G_CALLBACK (press_in_win_event),
				     buff);
    draw_canvas(buff);
  }
  /* ignore and reset the button */
  else gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget),FALSE);

  return TRUE;
}

gint expand_press_event (GtkWidget *widget, GdkEventButton *event,dbuff *buff)
{
  GdkRectangle rect;
  int sizex,sizey;
  int xval,yval;
  int i1,i2,j1,j2;

  i1=(int) (buff->disp.xx1 * (buff->param_set.npts-1) +.5);
  i2=(int) (buff->disp.xx2 * (buff->param_set.npts-1) +.5);

  j1=(int) (buff->disp.yy1 * (buff->npts2-1) +.5);
  j2=(int) (buff->disp.yy2 * (buff->npts2-1) +.5);

  sizex=buff->win.sizex;
  sizey=buff->win.sizey;
  xval=event->x;
  yval=event->y;
  yval=sizey+1-yval;
  if(yval==0) yval =1;
  if(yval ==sizey+1) yval=sizey;
  if (xval==0) xval =1;
  if (xval==sizex+1) xval=sizex;
  
  if(buff->win.press_pend==2){
    buff->win.press_pend--;
    if (buff->disp.dispstyle == SLICE_ROW){
      buff->win.pend1=(float) (xval-1)/(sizex-1) * (buff->disp.xx2-
		 buff->disp.xx1)+buff->disp.xx1;
    }
    else if (buff->disp.dispstyle == SLICE_COL){
      buff->win.pend1= (float)(xval-1)/(sizex-1)*(buff->disp.yy2-
	buff->disp.yy1)+buff->disp.yy1;
    }else if (buff->disp.dispstyle == RASTER){
      buff->win.pend1=(float) (xval-1)/(sizex-1) *(i2-i1+1.0)/(i2-i1)
	* (buff->disp.xx2-buff->disp.xx1)+buff->disp.xx1
	-0.5/(i2-i1+1.0)*(buff->disp.xx2-buff->disp.xx1);
      buff->win.pend3=(float) (yval-1)/(sizey-1) * (j2-j1+1.0)/(j2-j1)
	*(buff->disp.yy2-buff->disp.yy1)+buff->disp.yy1
	-0.5/(j2-j1+1.0)*(buff->disp.yy2-buff->disp.yy1);



    }
    draw_vertical(buff,&colours[BLUE],0.,(int) event->x);



    if (buff->disp.dispstyle == RASTER){ //draw line other way too !
      rect.x=1;
      rect.y=sizey+1-yval;
      rect.width=sizex;
      rect.height=1;
      gdk_draw_line(buff->win.pixmap,colourgc,1,
                  rect.y,sizex,rect.y);
      
      gtk_widget_queue_draw_area (widget, rect.x,rect.y,rect.width,rect.height);
    }

  }
  else if (buff->win.press_pend==1){
    buff->win.press_pend-=1;
    if(buff->disp.dispstyle==SLICE_ROW){
      buff->win.pend2=(float) (xval-1)/(sizex-1) 
	* (buff->disp.xx2-buff->disp.xx1)+buff->disp.xx1;
    }
    else if(buff->disp.dispstyle==SLICE_COL){
      buff->win.pend2=(float) (xval-1)/(sizex-1) 
      * (buff->disp.yy2-buff->disp.yy1)+buff->disp.yy1;
    }
    else if (buff->disp.dispstyle==RASTER){
      buff->win.pend2=(float) (xval-1)/(sizex-1) *(i2-i1+1.0)/(i2-i1)
	* (buff->disp.xx2-buff->disp.xx1)+buff->disp.xx1
	-0.5/(i2-i1+1.0)*(buff->disp.xx2-buff->disp.xx1);
      buff->win.pend4=(float) (yval-1)/(sizey-1) * (j2-j1+1.0)/(j2-j1)
	*(buff->disp.yy2-buff->disp.yy1)+buff->disp.yy1
	-0.5/(j2-j1+1.0)*(buff->disp.yy2-buff->disp.yy1);

      /*        buff->win.pend4=(float) (yval-1)/(sizey-1) * (buff->disp.yy2-
		       buff->disp.yy1)+buff->disp.yy1;
      */
    }

    /*    g_signal_disconnect_by_func(G_OBJECT (buff->win.canvas),
	  G_CALLBACK( expand_press_event),buff);*/
    /* don't need to do this, as setting the button to (in)active will*/

    
      if(buff->disp.dispstyle == SLICE_ROW || buff->disp.dispstyle==RASTER){
	buff->disp.xx2=MAX(buff->win.pend1,buff->win.pend2);
	buff->disp.xx1=MIN(buff->win.pend1,buff->win.pend2);
      }
      else if(buff->disp.dispstyle==SLICE_COL){
	buff->disp.yy2=MAX(buff->win.pend2,buff->win.pend1);
	buff->disp.yy1=MIN(buff->win.pend2,buff->win.pend1);
      }
      if (buff->disp.dispstyle ==RASTER){
	buff->disp.yy2=MAX(buff->win.pend3,buff->win.pend4);
	buff->disp.yy1=MIN(buff->win.pend3,buff->win.pend4);
      }
      buff->disp.yy2=MIN(buff->disp.yy2,1.0);
      buff->disp.yy1=MAX(buff->disp.yy1,0.0);
      buff->disp.xx2=MIN(buff->disp.xx2,1.0);
      buff->disp.xx1=MAX(buff->disp.xx1,0.0);

    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(buff->win.toggleb)
				 ,FALSE);
  }
  return TRUE;
    

}

gint expandf_routine(GtkWidget *widget,dbuff *buff)
{
  //printf("expandf routine\n");
  static char norecur = 0;
  if (norecur == 1){
    norecur = 0;
    return TRUE;
  }

  if (buff->scales_dialog != NULL){
    popup_msg("Can't expand while scales dialog open");
    norecur = 1;
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget),FALSE);
    norecur = 0;
    return TRUE;
  }

  if (buff->win.press_pend==0 &&GTK_TOGGLE_BUTTON(widget)->active
 && (buff->disp.dispstyle==SLICE_ROW ||buff->disp.dispstyle==SLICE_COL)){
    /* do the expand */
    buff->win.toggleb = widget;
    g_signal_handlers_block_by_func(G_OBJECT(buff->win.canvas),
				     G_CALLBACK (press_in_win_event),
				     buff);
    g_signal_connect (G_OBJECT (buff->win.canvas), "button_press_event",
                        G_CALLBACK( expandf_press_event), buff);
    buff->win.press_pend=1;
  }
  else if( !GTK_TOGGLE_BUTTON(widget)->active && buff->win.toggleb == widget){
    /* give up on expand */
    g_signal_handlers_disconnect_by_func(G_OBJECT (buff->win.canvas),
                                  G_CALLBACK( expandf_press_event),buff);
    g_signal_handlers_unblock_by_func(G_OBJECT(buff->win.canvas),
				     G_CALLBACK (press_in_win_event),
				     buff);

    buff->win.press_pend=0;
    draw_canvas(buff);
  }
  /* ignore and reset the button */
  else{
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget),FALSE);
    //    printf("expand_first, resetting\n");
  }

  return TRUE;
}
gint
expandf_press_event (GtkWidget *widget, GdkEventButton *event,dbuff *buff)
{
  int sizex,sizey;
  int xval;

  //printf("in expandf press event\n");
  sizex=buff->win.sizex;
  sizey=buff->win.sizey;

  xval=event->x;
  if(xval == 0) xval=1;
  if(xval==sizex+1) xval=sizex;

  if(buff->disp.dispstyle==SLICE_ROW){
    buff->disp.xx2=(float) (xval-1)/(sizex-1) * (buff->disp.xx2-buff->disp.xx1)
      +buff->disp.xx1;
    buff->disp.xx1=0;
  }
  if(buff->disp.dispstyle==SLICE_COL){
    buff->disp.yy2=(float) (xval-1)/(sizex-1) * (buff->disp.yy2-buff->disp.yy1)
      +buff->disp.yy1;
    buff->disp.yy1=0;
  }
  //printf("set xx1, xx2 to %f %f\n",buff->disp.xx1,buff->disp.xx2);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(buff->win.toggleb)
                                 ,FALSE);
  return TRUE;
    

}




gint Bbutton_routine(GtkWidget *widget,dbuff *buff)
{
  /*  if(!buff->win.press_pend){ */
    buff->disp.yscale *=4;
    unauto(buff); 
    buff->disp.yoffset /=4;
    draw_canvas(buff);
    /*  } */
    return 0;
}
gint bbutton_routine(GtkWidget *widget,dbuff *buff)
{
/*  if(!buff->win.press_pend){ */
    buff->disp.yscale *=1.25;
    unauto(buff); 
    buff->disp.yoffset /=1.25; 
    draw_canvas(buff);
    /*  } */
    return 0;
}
gint Sbutton_routine(GtkWidget *widget,dbuff *buff)
{
  /*   if(!buff->win.press_pend){ */
    buff->disp.yscale /=4;
    unauto(buff); 
    buff->disp.yoffset *=2; 
    draw_canvas(buff);
    /*  }*/

    return 0;
}

gint sbutton_routine(GtkWidget *widget,dbuff *buff)
{
  /*   if(!buff->win.press_pend){*/
    buff->disp.yscale /=1.25;
    unauto(buff); 
    buff->disp.yoffset *= 1.25;
    draw_canvas(buff);
    /*  } */

    return 0;
}

gint autocheck_routine(GtkWidget *widget,dbuff *buff)
{
  //printf("autocheck routine\n");
  if (GTK_TOGGLE_BUTTON(widget)->active) draw_canvas(buff);
  return 0;
}

gint do_auto(dbuff *buff)
{
  float max,min;
  int i1,i2,i,recadd;
  int flag;
  float spare;  

  max = 0;
  min = 0;

    recadd=buff->param_set.npts*2*buff->disp.record;

  i1= (int) (buff->disp.xx1*(buff->param_set.npts-1)+.5);
  i2= (int) (buff->disp.xx2*(buff->param_set.npts-1)+.5);

  flag=0;
  if(buff->disp.base){ 
    min=0;
    max=0;
    flag=1;
  }
  

  if (buff->disp.real){
    if (flag==0){
      max=buff->data[2*i1+recadd];
      min=max;
      flag=1;
    }
    for(i=i1;i<=i2;i++){
      if (buff->data[2*i+recadd] < min) min = buff->data[2*i+recadd];
      if (buff->data[2*i+recadd] > max) max = buff->data[2*i+recadd];
    }
  }
  if(buff->disp.imag){
    if(flag==0){
      max=buff->data[2*i1+1+recadd];
      min=max;
      flag=1;
    }
    for(i=i1;i<=i2;i++){
      if (buff->data[2*i+1+recadd] < min) min = buff->data[2*i+1+recadd];
      if (buff->data[2*i+1+recadd] > max) max = buff->data[2*i+1+recadd];
    }
  }
  if(buff->disp.mag){
    if(flag==0){
      max=sqrt(buff->data[2*i1+1+recadd]*buff->data[2*i1+1+recadd]+
	       buff->data[2*i1+recadd]*buff->data[2*i1+recadd]);
      min=max;
      flag=1;
    }
    for(i=i1;i<=i2;i++){
      spare=sqrt(buff->data[2*i+1+recadd]*buff->data[2*i+1+recadd]
		 +buff->data[2*i+recadd]*buff->data[2*i+recadd]);
      if (spare < min) min = spare;
      if (spare > max) max = spare;
    }
  }

  if (min==max || flag==0){
    buff->disp.yoffset=0.0;
    buff->disp.yscale=1.0;
  }
  else{
    buff->disp.yscale=2.0/(max-min)/1.1;
    buff->disp.yoffset=-(max+min)/2.0;
  }
  return 0;
}

gint do_auto2(dbuff *buff)
{
  float max,min;
  int i1,i2,i,recadd;
  int flag;
  float spare; 

  min = 0;
  max = 0;

  
    i1= (int) (buff->disp.yy1*(buff->npts2-1)+.5);
    i2= (int) (buff->disp.yy2*(buff->npts2-1)+.5);

    if (buff->is_hyper && i1 %2 ==1) i1-=1;
    if (buff->is_hyper && i2 %2 ==1) i2-=1;
    
    recadd=2*buff->param_set.npts;

  flag=0;

  if(buff->disp.base){ 
    min=0;
    max=0;
    flag=1;
  }

  if (buff->disp.real){
    if ( flag == 0 ){
      max=buff->data[i1*recadd+2*buff->disp.record2];
      min=max;
      flag=1;
    }
    for(i=i1;i<=i2;i+=1+buff->is_hyper){
      if (buff->data[recadd*i+2*buff->disp.record2] < min) 
	min = buff->data[recadd*i+2*buff->disp.record2];
      if (buff->data[recadd*i+2*buff->disp.record2] > max) 
	max = buff->data[recadd*i+2*buff->disp.record2];
    }
  }
  if(buff->disp.imag && buff->is_hyper){
    if(flag==0){
      max=buff->data[recadd*(i1+1)+2*buff->disp.record2];
      min=max;
      flag=1;
    } 
    for(i=i1;i<=i2;i+=1+buff->is_hyper){
      spare=buff->data[recadd*(i+1)+2*buff->disp.record2];
      if (spare < min) min = spare;
      if (spare > max) max = spare;
    }
  }
  if(buff->disp.mag && buff->is_hyper){
    if(flag==0){
      spare=sqrt(buff->data[recadd*(i1+1)+2*buff->disp.record2]
		 *buff->data[recadd*(i1+1)+2*buff->disp.record2]
		 +buff->data[recadd*i1+2*buff->disp.record2]
		 *buff->data[recadd*i1+2*buff->disp.record2]);
      max=spare;
      min=max;
      flag=1;
    } 
    for(i=i1;i<=i2;i+=1+buff->is_hyper){
      spare=sqrt(buff->data[recadd*(i+1)+2*buff->disp.record2]
		 *buff->data[recadd*(i+1)+2*buff->disp.record2]
		 +buff->data[recadd*i+2*buff->disp.record2]
		 *buff->data[recadd*i+2*buff->disp.record2]);
      if (spare < min) min = spare;
      if (spare > max) max = spare;
    }
  } 
  
  if ((min==max) || (flag==0)){
    buff->disp.yoffset=0.0;
    buff->disp.yscale=1.0;
  }
  else{
    buff->disp.yscale=2.0/(max-min)/1.1;
    buff->disp.yoffset=-(max+min)/2.0;
  }
  return 0;
}


gint auto_routine(GtkWidget *widget,dbuff *buff)
{ 
  if (buff->disp.dispstyle ==SLICE_ROW)
    do_auto(buff);
  if (buff->disp.dispstyle ==SLICE_COL)
    do_auto2(buff);
  
  draw_canvas(buff);
  return 0;
}

gint offset_routine(GtkWidget *widget,dbuff *buff)
{
  //printf("offset routine\n");
  if (buff->win.press_pend==0 &&GTK_TOGGLE_BUTTON(widget)->active
 && (buff->disp.dispstyle==SLICE_ROW ||buff->disp.dispstyle==SLICE_COL)){
    buff->win.toggleb = widget;
    buff->win.press_pend=2;
    g_signal_handlers_block_by_func(G_OBJECT(buff->win.canvas),
				     G_CALLBACK (press_in_win_event),
				     buff);
    g_signal_connect (G_OBJECT (buff->win.canvas), "button_press_event",
                        G_CALLBACK( offset_press_event), buff);
  }
  else if( !GTK_TOGGLE_BUTTON(widget)->active && buff->win.toggleb==widget){
    g_signal_handlers_disconnect_by_func(G_OBJECT (buff->win.canvas),
                                  G_CALLBACK( offset_press_event),buff);
    buff->win.press_pend=0;
    g_signal_handlers_unblock_by_func(G_OBJECT(buff->win.canvas),
				     G_CALLBACK (press_in_win_event),
				     buff);
    draw_canvas(buff);
  }
  else gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget),FALSE);
  return TRUE;
}
gint offset_press_event (GtkWidget *widget, GdkEventButton *event,dbuff *buff)
{
  GdkRectangle rect;
  int sizex,sizey;
  int yval;

  sizex=buff->win.sizex;
  sizey=buff->win.sizey;
  yval=event->y;
  if (yval==0) yval=1;
  if (yval==sizey+1) yval=sizey;

  if(buff->win.press_pend==2){
    buff->win.press_pend--;
    buff->win.pend1= (float) (yval-1) /(sizey-1);

    rect.x=1;
    rect.y=yval;
    rect.width=sizex;
    rect.height=1;
    gdk_gc_set_foreground(colourgc,&colours[BLUE]);
    gdk_draw_line(buff->win.pixmap,colourgc,rect.x,
                  rect.y,sizex,rect.y);
    gtk_widget_queue_draw_area (widget, rect.x,rect.y,rect.width,rect.height);

  }
  else if (buff->win.press_pend==1){ /* second time through */
    unauto(buff);
    buff->win.press_pend--;
    buff->win.pend2=(float) (yval-1)/(sizey-1);
    /*    g_signal_handlers_disconnect_by_func(G_OBJECT (buff->win.canvas),
	  G_CALLBACK( offset_press_event),buff);  */

    buff->disp.yoffset +=(buff->win.pend1-buff->win.pend2)
      /buff->disp.yscale*2.0;
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(buff->win.toggleb)
                                 ,FALSE);
  }
  return TRUE;
    

}



void make_active(dbuff *buff){
    //printf("trying to raise\n");
    // if acq is in progress, change the order
  // I think this is to avoid "changing parameter during acquisition errors

    if (buff->buffnum== upload_buff && acq_in_progress == ACQ_RUNNING){
      show_parameter_frame( &buff->param_set );
      show_process_frame( buff->process_data );
      last_current = current;
      current = buff->buffnum;
      show_active_border(); 
      gdk_window_raise(buff->win.window->window);
      update_2d_buttons();
    }else{
      last_current = current;
      current = buff->buffnum;
      show_active_border(); 
      gdk_window_raise(buff->win.window->window);
      show_parameter_frame( &buff->param_set );
      show_process_frame( buff->process_data );
      update_2d_buttons_from_buff( buff );
    }
}




gint press_in_win_event(GtkWidget *widget,GdkEventButton *event,dbuff *buff)
{
 
  char title[UTIL_LEN];
  int wx,wy;

  static float last_freq=0,last_freq2=0;
  static unsigned int last_point=0,last_point2;
  double dwell2,sw2;
  double new_freq,new_freq2;
  int i;
   /* we will get spurious releases after expand and offset events */


  if (event->type ==GDK_BUTTON_PRESS){

    if (buff->disp.dispstyle==SLICE_ROW){
      buff->disp.record2=pix_to_x(buff,event->x);
    }

    if(buff->disp.dispstyle==SLICE_COL){
      buff->disp.record=pix_to_x(buff,event->x);

      // if it returns an even number, make it odd

      if(buff->is_hyper && buff->disp.record %2 ==1) buff->disp.record-=1;

    }
    if(buff->disp.dispstyle==RASTER){
      buff->disp.record2=pix_to_x(buff,event->x);
      buff->disp.record=pix_to_y(buff,event->y);

      if (buff->is_hyper && buff->disp.record %2 ==1) buff->disp.record -=1;

    }
    //    snprintf(title,UTIL_LEN,"pixels: %i %i",(int) event->x,(int) event->y);

    // first line in dialog: value of real

    snprintf(title,UTIL_LEN,"real: %f",buff->data[2*buff->disp.record2
	      +buff->disp.record*buff->param_set.npts*2]);

    gtk_label_set_text(GTK_LABEL(fplab1),title);

    //printf("rec,rec2: %i, %i\n",buff->disp.record,buff->disp.record2);


    // second line: magnitude - only if its appropriate

    if (buff->disp.dispstyle == SLICE_ROW || buff->disp.dispstyle == RASTER){
      snprintf(title,UTIL_LEN,"Magnitude: %f",sqrt(pow(buff->data[2*buff->disp.record2
						       +buff->disp.record*buff->param_set.npts*2],2)+
					 pow(buff->data[2*buff->disp.record2+buff->disp.record*buff->param_set.npts*2+1],2)));
    }

    if (buff->disp.dispstyle == SLICE_COL && buff->is_hyper){
      snprintf(title,UTIL_LEN,"Magnitude: %f",sqrt(pow(buff->data[2*buff->disp.record2
						       +buff->disp.record*buff->param_set.npts*2],2)+
					 pow(buff->data[2*buff->disp.record2+(buff->disp.record+1)*buff->param_set.npts*2],2)));
    }

    if (buff->disp.dispstyle == SLICE_COL && buff->is_hyper == 0)
      snprintf(title,UTIL_LEN,"Magnitude: none");
    
    gtk_label_set_text(GTK_LABEL(fplab2),title);


    // third line, which data points we're looking at
    snprintf(title,UTIL_LEN,"np1d: %i, np2d %i",buff->disp.record2,buff->disp.record);
    gtk_label_set_text(GTK_LABEL(fplab3),title);



    // fourth line - time or freq in second dimension
    // for 2nd dimension, get the dwell or sw.
    sw2=0;
    i = pfetch_float(&buff->param_set,"dwell2",&dwell2,0);
    if (i == 1)
      sw2 =1/dwell2;
    else
      pfetch_float(&buff->param_set,"sw2",&sw2,0);
    if (i==1)
      dwell2=1/sw2;
    if (sw2 == 0){
      snprintf(title,UTIL_LEN," ");
    }
    else{
      // ok, so there is a dwell or sw in the 2nd D.
      new_freq2 = -( (double) buff->disp.record*sw2/buff->npts2 - (double) sw2/2.);

      if (buff->flags & FT_FLAG2)
	snprintf(title,UTIL_LEN,"%8.1f Hz delta: %8.1f Hz",
		new_freq2,new_freq2-last_freq2);
      
      else // its still in time steps in 2nd D
	snprintf(title,UTIL_LEN,"%g us delta: %g us",buff->disp.record*dwell2*1e6/(1+buff->is_hyper),
		((double)buff->disp.record-(double) last_point2)*dwell2*1e6/(1+buff->is_hyper));
      
      last_point2=buff->disp.record;
      last_freq2=new_freq2;
    }
    gtk_label_set_text(GTK_LABEL(fplab4),title);


      // fifth line the frequency or time in direct dimension
    new_freq = - ( (double) buff->disp.record2 * buff->param_set.sw/buff->param_set.npts
		 - (double) buff->param_set.sw/2.);
    if (buff->flags & FT_FLAG)
      snprintf(title,UTIL_LEN,"%8.1f Hz delta: %8.1f Hz",
	      new_freq,new_freq-last_freq);
    
    else
      snprintf(title,UTIL_LEN,"%g us delta: %g us",
	      buff->disp.record2 * buff->param_set.dwell,
	      (    (double)buff->disp.record2-(double) last_point)*buff->param_set.dwell);
    last_point=buff->disp.record2;
    last_freq=new_freq;
  
    gtk_label_set_text(GTK_LABEL(fplab5),title);


    /* get position of buffer window */
    gdk_window_get_position(buff->win.window->window,&wx,&wy);
    /* and set position of popup */

    gtk_window_move(GTK_WINDOW(freq_popup),wx+event->x+1,wy+event->y+25);
    gtk_widget_show(freq_popup);

  }
  if (event->type ==GDK_BUTTON_RELEASE){
    gtk_widget_hide(freq_popup);
  }

  /* if its a press and our window isn't active, make it active */

  if (buff->buffnum != current && event->type ==GDK_BUTTON_PRESS){
    make_active(buff);

  } else if (event->type == GDK_BUTTON_PRESS && (acq_in_progress != ACQ_RUNNING || buff->buffnum != upload_buff))
    update_2d_buttons_from_buff(buff);
    
  snprintf(title,UTIL_LEN,"p1: %u",buff->disp.record);
  gtk_label_set_text(GTK_LABEL(buff->win.p1_label),title);
  snprintf(title,UTIL_LEN,"p2: %u",buff->disp.record2);
  gtk_label_set_text(GTK_LABEL(buff->win.p2_label),title);

  return 0;
}



void show_active_border()
{
  dbuff *buff;
  GdkRectangle rect;


 buff=buffp[current];
 if(buff != NULL){
    gdk_gc_set_foreground(colourgc,&colours[RED]);
   /* draw border in red */
    /*left edge */
   rect.x=0;
   rect.y=0;
   rect.height=buff->win.sizey+2;
   rect.width=1;
   gdk_draw_line(buff->win.pixmap,colourgc,
		   rect.x,rect.y,rect.x,rect.y+rect.height);
   gtk_widget_queue_draw_area(buff->win.canvas,rect.x,rect.y,rect.width,rect.height);

   rect.x=buff->win.sizex+1;
   gdk_draw_line(buff->win.pixmap,colourgc,
		   rect.x,rect.y,rect.x,rect.y+rect.height);
   gtk_widget_queue_draw_area(buff->win.canvas,rect.x,rect.y,rect.width,rect.height);

   rect.y=0;
   rect.x=0;
   rect.height=1;
   rect.width=buff->win.sizex+2;
   gdk_draw_line(buff->win.pixmap,colourgc,
		   rect.x,rect.y,rect.x+rect.width,rect.y);
   gtk_widget_queue_draw_area(buff->win.canvas,rect.x,rect.y,rect.width,rect.height);
   
   rect.y=buff->win.sizey+1;
   gdk_draw_line(buff->win.pixmap,colourgc,
		   rect.x,rect.y,rect.x+rect.width,rect.y);
   gtk_widget_queue_draw_area(buff->win.canvas,rect.x,rect.y,rect.width,rect.height);

 }
 buff=buffp[last_current];
 if((buff != NULL) && (last_current !=current)){
    /*left edge */
   rect.x=0;
   rect.y=0;
   rect.height=buff->win.sizey+2;
   rect.width=1;
   gdk_draw_line(buff->win.pixmap,buff->win.canvas->style->white_gc,
		   rect.x,rect.y,rect.x,rect.y+rect.height);
   gtk_widget_queue_draw_area(buff->win.canvas,rect.x,rect.y,rect.width,rect.height);

   rect.x=buff->win.sizex+1;
   gdk_draw_line(buff->win.pixmap,buff->win.canvas->style->white_gc,
		   rect.x,rect.y,rect.x,rect.y+rect.height);
   gtk_widget_queue_draw_area(buff->win.canvas,rect.x,rect.y,rect.width,rect.height);

   rect.y=0;
   rect.x=0;
   rect.height=1;
   rect.width=buff->win.sizex+2;
   gdk_draw_line(buff->win.pixmap,buff->win.canvas->style->white_gc,
		   rect.x,rect.y,rect.x+rect.width,rect.y);
   gtk_widget_queue_draw_area(buff->win.canvas,rect.x,rect.y,rect.width,rect.height);
   
   rect.y=buff->win.sizey+1;
   gdk_draw_line(buff->win.pixmap,buff->win.canvas->style->white_gc,
		   rect.x,rect.y,rect.x+rect.width,rect.y);
   gtk_widget_queue_draw_area(buff->win.canvas,rect.x,rect.y,rect.width,rect.height);

 }

 return;
}

gint hyper_check_routine(GtkWidget *widget,dbuff *buff)
{
  //printf("in hyper_check \n");
  static int norecur = 0;
  char title[UTIL_LEN];

  if (norecur == 1) {
    norecur=0;
    return TRUE;
  }

  if(buff->win.press_pend >0){
    norecur=1;
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget),buff->is_hyper);
    return TRUE;
  }
  if(GTK_TOGGLE_BUTTON(widget)->active){

    /* ok, just said that it is hypercomplex */
    if (buff->npts2==1){
      /* if there's only one point, forget it */
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget),FALSE);
      return TRUE;
    }

    // if phase window is open for our buff, get lost
    if (phase_data.is_open == 1 && phase_data.buffnum == buff->buffnum){
      norecur=1;
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget),buff->is_hyper);
    }

    
    /* if npts is odd, deal with it */
    if ( buff->npts2 %2 ==1){
      buff->npts2 -=1; /* throw away last record */
      buff->data=g_realloc(buff->data,2*4*buff->param_set.npts*buff->npts2);
      //printf("points are odd, killing last\n");
    }
    // if we're on an odd record, deal with it 
    if (buff->disp.record %2 ==1){
      buff->disp.record -=1;
      snprintf(title,UTIL_LEN,"p1: %u",buff->disp.record);
      gtk_label_set_text(GTK_LABEL(buff->win.p1_label),title);
    }
    if(buff->is_hyper) printf("hyper was already TRUE!!!\n");
    buff->is_hyper=TRUE;
    draw_canvas(buff);
    return TRUE;
  }

  /* that's it if we made it hypercomplex,
     otherwise undo the above, but we could be arriving here 
     just for ticking the box incorrectly */
  if(buff->win.press_pend > 0 || buff->npts2==1) return TRUE;
  buff->is_hyper = FALSE;
  draw_canvas(buff);
  return TRUE;
     

}

gint plus_button(GtkWidget *widget,dbuff *buff){
char title[UTIL_LEN];

  if(buff->disp.dispstyle ==SLICE_ROW)
    if (buff->disp.record < buff->npts2-(buff->is_hyper+1)) {
      buff->disp.record += 1+buff->is_hyper;
      if (buff->buffnum == current) update_2d_buttons_from_buff( buff );
    }
  if(buff->disp.dispstyle ==SLICE_COL)
    if (buff->disp.record2 < buff->param_set.npts-1)
      buff->disp.record2 += 1;
  draw_canvas(buff); 

  snprintf(title,UTIL_LEN,"p1: %u",buff->disp.record);
  gtk_label_set_text(GTK_LABEL(buff->win.p1_label),title);
  snprintf(title,UTIL_LEN,"p2: %u",buff->disp.record2);
  gtk_label_set_text(GTK_LABEL(buff->win.p2_label),title);

  return 0;
}

gint minus_button(GtkWidget *widget,dbuff *buff){
char title[UTIL_LEN];
  if(buff->disp.dispstyle ==SLICE_ROW)
    if (buff->disp.record > buff->is_hyper) {
      buff->disp.record -= (1+buff->is_hyper);
      if (buff->buffnum == current) update_2d_buttons_from_buff( buff );
    }
  if(buff->disp.dispstyle ==SLICE_COL)
    if (buff->disp.record2 >0)
      buff->disp.record2 -= 1;
  draw_canvas(buff);

  snprintf(title,UTIL_LEN,"p1: %u",buff->disp.record);
  gtk_label_set_text(GTK_LABEL(buff->win.p1_label),title);
  snprintf(title,UTIL_LEN,"p2: %u",buff->disp.record2);
  gtk_label_set_text(GTK_LABEL(buff->win.p2_label),title);

  return 0;
}

gint row_col_routine(GtkWidget *widget,dbuff *buff){


  if (buff->win.press_pend >0 || phase_data.is_open == 1) return TRUE;


  if(strncmp(gtk_label_get_text(GTK_LABEL(buff->win.row_col_lab)),"Row",3)==0 && buff->npts2>1){
    gtk_label_set_text(GTK_LABEL(buff->win.row_col_lab),"Column");
    if (buff->disp.dispstyle==SLICE_ROW)
    buff->disp.dispstyle=SLICE_COL;
  }
  else{ /* change from column to row */
    gtk_label_set_text(GTK_LABEL(buff->win.row_col_lab),"Row");
    if (buff->disp.dispstyle==SLICE_COL)
      buff->disp.dispstyle=SLICE_ROW;
  }

  // this doesn't seem necessary?  I wonder if it is?
  //  if (buff->buffnum == current) update_2d_buttons_from_buff( buff );

  // don't bother to draw the screen if we're looking at a 2d view
  if ( buff->disp.dispstyle == SLICE_ROW ||
       buff->disp.dispstyle == SLICE_COL) draw_canvas(buff);

  return TRUE;

}


gint slice_2D_routine(GtkWidget *widget,dbuff *buff){

 
//printf("slice routine\n");
 
 if(buff->disp.dispstyle==RASTER){
   /* if its a raster, check to see if we should do row or col */
   
   if(strncmp(gtk_label_get_text(GTK_LABEL(buff->win.row_col_lab)),"Row",3)==0) 
     buff->disp.dispstyle=SLICE_ROW;
   else buff->disp.dispstyle=SLICE_COL;
   gtk_label_set_text(GTK_LABEL(buff->win.slice_2d_lab),"Slice");
 }
 else if (buff->npts2 > 1){
   buff->disp.dispstyle=RASTER;
   gtk_label_set_text(GTK_LABEL(buff->win.slice_2d_lab),"2D");
 }
 // if (buff->buffnum == current) update_2d_buttons_from_buff( buff );
 draw_canvas(buff);
 return 0;
}

gint buff_resize( dbuff* buff, int npts1, int npts2 )

{
  int i,j;
  float *data_old;
  if( (buff->param_set.npts != npts1) ||  ( buff->npts2 != npts2 ) ) {
    //       printf("doing buff resize to %i x %i\n",npts1,npts2);

    // only change the record we're viewing if we have to.
    // this was the cause of a long standing bug: used to only check for > 
    // rather than >= !!!!!
    if (buff->disp.record >= npts2) buff->disp.record = 0;
    if (buff->disp.record2 >= npts1) buff->disp.record2 = 0;


    data_old=buff->data;
    buff->data=g_malloc(2*npts1*npts2*sizeof( float ));
    //    printf("buff resize: malloc for resize\n");
    if (buff->data == NULL){
      char title[UTIL_LEN];
      buff->data=data_old;
      printf("buff resize: MALLOC ERROR\n");
      snprintf(title,UTIL_LEN,"buff resize: MALLOR ERROR\nasked for npts %i npts2 %i",npts1,npts2);
      popup_msg(title);
      return 0;
    }

    /* now copy data from old buffer to new one as best we can */

    for (i=0 ; i< MIN(npts2,buff->npts2) ; i++){
      for (j=0 ; j<MIN(npts1,buff->param_set.npts) ; j++){
	buff->data[2*j+i*2*npts1]=data_old[2*j+i*2*buff->param_set.npts];
	buff->data[2*j+1+i*2*npts1]=data_old[2*j+1+i*2*buff->param_set.npts];
      }
      for(j=MIN(npts1,buff->param_set.npts);j<npts1;j++){
	buff->data[2*j+i*2*npts1]=0;
	buff->data[2*j+1+i*2*npts1]=0;
      }
      
    }
    for(i=MIN(npts2,buff->npts2);i<npts2;i++){
      for(j=0;j<npts1;j++){
	buff->data[2*j+i*2*npts1]=0;
	buff->data[2*j+1+i*2*npts1]=0;
      }

    }

    g_free(data_old);
    buff->param_set.npts = npts1;
    buff->npts2 = npts2;
  }

  if( npts2 <= 1 ) {
    buff->disp.dispstyle = SLICE_ROW;
  }  

  // deal with add_sub stuff
  i = gtk_combo_box_get_active(GTK_COMBO_BOX(add_sub.s_buff1));
  if (add_sub.index[i] == buff->buffnum){
    add_sub_changed(add_sub.s_buff1,NULL);
    //    printf("resize: first source #records changed\n");
  }
  i = gtk_combo_box_get_active(GTK_COMBO_BOX(add_sub.s_buff2));
  if (add_sub.index[i] == buff->buffnum){
    add_sub_changed(add_sub.s_buff2,NULL);
    //    printf("resize: second source #records changed\n");
  }
  i = gtk_combo_box_get_active(GTK_COMBO_BOX(add_sub.dest_buff));
  if (i>0) {
    if (add_sub.index[i-1] == buff->buffnum){
      add_sub_changed(add_sub.dest_buff,NULL);
      //      printf("resize: dest #records changed\n");
    }
  }



   return 0;
}



gint do_load_wrapper( GtkWidget* widget, GtkFileSelection* fs )

{
  char path[PATH_LENGTH];
  dbuff* buff;
  int result;


  path_strcpy(path, gtk_file_selection_get_filename ( GTK_FILE_SELECTION(fs) ));
  buff =  (dbuff *)  g_object_get_data( G_OBJECT( fs ), BUFF_KEY );
  //make sure the buffer still exists!

  if (buff == NULL){
    popup_msg("buffer was destroyed, can't open");
    //    gtk_widget_destroy(GTK_WIDGET(widget));
    return TRUE;
  }

  // here we need to strip out a potential trailing /
  if (path[strlen(path)-1] == '/' ) path[strlen(path)-1] = 0;


  result = do_load( buff, path);
    
  gtk_widget_hide( GTK_WIDGET( fs ) );
  gtk_widget_destroy( GTK_WIDGET( fs ) );
  return result;
}

gint do_load( dbuff* buff, char* path )
{
 
  char p[ PARAMETER_LEN ] = "";
  char s[PATH_LENGTH],ch1,ch2;
  char fileN[ PATH_LENGTH ];
  FILE* fstream;
  int i,fl1,fl2,j,flag;
  unsigned int new_npts1=0;
  unsigned int new_npts2=0;
  unsigned int new_acq_npts;
  unsigned long sw,acqns;
  float dwell;

  //  printf( "do_load: got path: %s while current dir is: %s\n", path,getcwd(fileN,PATH_LENGTH));

  // put path into path spot

  // but only if this isn't the users temp file.
  //  printf("in do_load: %s\n",path);
  path_strcpy(s,getenv("HOME"));
  path_strcat(s,"/Xnmr/data/acq_temp");
  if (strcmp(s,path) !=0 ){
 
    // and the whole thing will go into the reload spot at the end
    put_name_in_buff(buff,path);
    //     printf("do_load: put %s in save_path\n",buff->param_set.save_path);
  }
  //  else printf("not putting path into save path, because its the acq_temp file\n");
  
  path_strcpy( fileN, path);
  path_strcat( fileN, "/params" );

  //printf( "opening parameter file: %s\n", fileN );
  fstream = fopen( fileN, "r" );

  if( fstream == NULL ) {
    popup_msg("File not found");
    return -1;
  }

  fscanf( fstream, "%s\n", buff->param_set.exec_path );
  fscanf( fstream, 
     "npts = %u\nacq_npts = %u\nna = %lu\nna2 = %u\nsw = %lu\ndwell = %f\n", 
	  &new_npts1,&new_acq_npts,&acqns, &new_npts2 ,&sw,&dwell);
  //  printf("do_load: np1 %u na %u np2 %u sw: %lu dwell: %f\n",new_npts1,acqns,new_npts2,sw,dwell);
  buff_resize( buff, new_npts1, new_npts2 );

  //this resets the acq_npts in the buff struct, so fix it 
  buff->acq_npts=new_acq_npts;

  if( buff->npts2 > 1 )
    buff->disp.dispstyle = RASTER;
  else
    buff->disp.dispstyle = SLICE_ROW;


  // ct was added late to the param file, it may or may not be there.
  // as were ch1 and ch2
  buff->ct = 0;

  flag = 0;
  do{
    if (fgets(s,PATH_LENGTH,fstream) == NULL) flag = 0;
    else{
      if (strncmp(s,"ct = ",5) == 0){
	sscanf(s,"ct = %lu\n",&buff->ct);
	flag = 1;
	//	printf("found ct, value = %lu\n",buff->ct);
      }
      else if (strncmp(s,"ch1 = ",6) == 0){
	sscanf(s,"ch1 = %c\n",&ch1);
	flag = 1;
	set_ch1(buff,ch1);
	//set channel 1
	//	printf("do_load found ch1: %c\n",ch1);
      }
      else if (strncmp(s,"ch2 = ",6) == 0){
	sscanf(s,"ch2 = %c\n",&ch2);
	flag = 1;
	set_ch2(buff,ch2);
	//	printf("do_load found ch2: %c\n",ch2);
	//set channel 2
      }
      else{
	flag = 0; // had a non-null string that wasn't a ct or a ch
	strncat(p,s,PATH_LENGTH); // add our string to the param string
      }
    }
  }while (flag == 1);



  snprintf(s,PATH_LENGTH,"ct: %lu",buff->ct);
  gtk_label_set_text(GTK_LABEL(buff->win.ct_label),s);
      

  while( fgets( s, PATH_LENGTH, fstream ) != NULL )
    strncat( p, s, PATH_LENGTH );

  fclose( fstream );

  path_strcpy( fileN, buff->param_set.exec_path);




  load_param_file( fileN, &buff->param_set );
  load_p_string( p, buff->npts2, &buff->param_set );
  buff->param_set.num_acqs = acqns;
  buff->npts2 = buff->npts2;
  buff->param_set.dwell=dwell;
  buff->param_set.sw=sw;
  buff->param_set.num_acqs_2d=buff->npts2;

  buff->phase0_app =0;
  buff->phase1_app =0;
  buff->phase20_app =0;
  buff->phase21_app =0;
  //  buff->process_data[PH].val = GLOBAL_PHASE_FLAG ;  why ?
     
  // load the proc_parameters
  path_strcpy( fileN, path);
  path_strcat( fileN, "/proc_params" );
  fstream = fopen( fileN, "r" );
  
  if( fstream != NULL ) {
    
    fscanf(fstream,"PH: %f %f %f %f\n",&buff->phase0_app,&buff->phase1_app,
	   &buff->phase20_app,&buff->phase21_app);
    //    printf("in do_load read phases of: %f %f %f %f\n",buff->phase0_app,buff->phase1_app,
    //	   buff->phase20_app,buff->phase21_app);
    fl1=0;
    fl2=0;
    fscanf(fstream,"FT: %i\n",&fl1);
    fscanf(fstream,"FT2: %i\n",&fl2); //why doesn't this work???
    //    printf("in do_load, read ft_flag of: %i %i\n",fl1,fl2);
    fclose( fstream );
    //    printf("flags: %i\n",buff->flags);
    buff->flags = fl1 | fl2 ;
    //    printf("flags: %i\n",buff->flags);
  }
  else{
    // assume its time domain in both dimensions.
    fl1=0;
    fl2=0;
    buff->flags = fl1|fl2;
  }
  
  //Now we have to load the data, this is easy
  
  path_strcpy( fileN, path);
  path_strcat( fileN, "/data" );
  
  fstream = fopen( fileN, "r" );
  
  if( fstream == NULL ) {
    perror( "do_load: couldn't open data file" );
    return -1;
  }
  
  i=fread( buff->data, sizeof( float ), buff->param_set.npts*buff->npts2*2, fstream );
  
  //  printf("read %i points\n",i);
  fclose( fstream );

  // check to make sure we read in all the points.  If not, zero out what's left.
  if (i < buff->param_set.npts*buff->npts2*2)
    for (j=i;j< buff->param_set.npts*buff->npts2*2;j++)
      buff->data[j]=0.;


  if (buff->buffnum ==current){
    show_parameter_frame( &buff->param_set );
    show_process_frame( buff->process_data);
    update_2d_buttons_from_buff( buff );
  }
  draw_canvas( buff );
  path_strcpy(buff->path_for_reload,path);
  set_window_title(buff);
  
  return 0;
  
}

gint check_overwrite_wrapper( GtkWidget* widget, GtkFileSelection* fs )
{
  char path[PATH_LENGTH];
  dbuff* buff;
  gint result;

  path_strcpy(path,gtk_file_selection_get_filename ( fs ));
  buff = (dbuff*) g_object_get_data( G_OBJECT( fs ), BUFF_KEY );

  //  printf("from check_overwrite_wrapper (file_save dialog): %s\n",path);

  // assume this is always a data file even if has a trailing /
  if (path[strlen(path)-1] == '/') path[strlen(path)-1]=0;
  //  printf("from check_overwrite_wrapper (file_save dialog): %s\n",path);

  result = check_overwrite( buff, path );



  gtk_widget_hide( GTK_WIDGET( fs ) );
  gtk_widget_destroy( GTK_WIDGET( fs ) );

  return result;
}

gint check_overwrite( dbuff* buff, char* path )
{
  GtkWidget *dialog;
  GtkWidget *label;
  GtkWidget *yes_b;
  GtkWidget *no_b;
  
  // if check_overwrite gets something with a trailing /, it should barf.

  if (path[strlen(path)-1] == '/'){
    popup_msg("Invalid filename");
    return 0;
  }
  //  printf("in check_overwrite got path: %s\n",path);

  if( mkdir( path, S_IRWXU | S_IRWXG | S_IRWXO ) < 0 ) {
    if( errno != EEXIST ) {
      popup_msg("check_overwrite can't mkdir?");
      return 0 ;
    }
    else // does exist...
      {
	// if it does exist, store the proposed filename in stored_path, 
	// and store the buff in stored_buffer.
    path_strcpy(stored_path,path);
    stored_buff = buff;

    dialog = gtk_dialog_new();
    gtk_window_set_modal( GTK_WINDOW( dialog ), TRUE );

    label = gtk_label_new ("File Exists, overwrite? ");

    yes_b = gtk_button_new_with_label("Yes");
    no_b = gtk_button_new_with_label("No");
 
    g_signal_connect (G_OBJECT (yes_b), "clicked", G_CALLBACK (do_save_wrapper), dialog);
    g_signal_connect_swapped (G_OBJECT (no_b), "clicked", G_CALLBACK (gtk_widget_destroy), G_OBJECT( dialog ) );

    gtk_box_pack_start (GTK_BOX ( GTK_DIALOG(dialog)->action_area ),yes_b,FALSE,TRUE,0);
    gtk_box_pack_start (GTK_BOX ( GTK_DIALOG(dialog)->action_area ),no_b,FALSE,TRUE,0);

    gtk_container_add (GTK_CONTAINER (GTK_DIALOG(dialog)->vbox), label);

    g_object_set_data( G_OBJECT( yes_b ), BUFF_KEY, buff );
    g_object_set_data( G_OBJECT( yes_b ), PATH_KEY, path );

    gtk_window_set_transient_for(GTK_WINDOW(dialog),GTK_WINDOW(buff->win.window));
    gtk_window_set_position(GTK_WINDOW(dialog),GTK_WIN_POS_CENTER_ON_PARENT);


    gtk_widget_show_all (dialog);

      }
  }
  else
    do_save( buff, path );

    
  
  

  return 0;

}

gint do_save_wrapper( GtkWidget* widget, GtkWidget* dialog )

{
  gint result;

  if( stored_buff == NULL ) {
    printf( "do_save_wrapper can't retrieve buff\n" );
    return -1;
  }

 if( stored_path == NULL ) {
    printf( "do_save_wrapper can't retrieve path\n" );
    return -1;
  }

 // printf("going through do_save_wrapper with path: %s\n",path);
  result = do_save( stored_buff, stored_path );

  gtk_widget_destroy( dialog );

  return result;
}

gint do_save( dbuff* buff, char* path )
{

  FILE* fstream;
  char fileN[ PATH_LENGTH ];
  char s[ PARAMETER_LEN ];
  int temp_npts2;
  // printf( "do_save: got path: %s while current dir is: %s\n ", path,getcwd(fileN,PATH_LENGTH));

  path_strcpy( fileN, path);
  path_strcat( fileN, "/params" );

  //  printf( "creating parameter file: %s\n", fileN );
  fstream = fopen( fileN, "w" );
  fprintf( fstream, "%s\n", buff->param_set.exec_path ); 
  //  printf("putting: %s as exec path\n",buff->param_set.exec_path);
  //  printf("in do_save, dwell is: %f\n",buff->param_set.dwell);
  fprintf( fstream, 
     "npts = %u\nacq_npts = %u\nna = %lu\nna2 = %u\nsw = %lu\ndwell = %f\nct = %lu\n", 
	   buff->param_set.npts, buff->acq_npts,buff->param_set.num_acqs, 
	   buff->npts2 ,buff->param_set.sw,buff->param_set.dwell,buff->ct);

  fprintf( fstream,"ch1 = %c\nch2 = %c\n",get_ch1(buff),get_ch2(buff));

  /* need to fiddle with the num of points in 2nd D so that 
    make_param_string gives us the right stuff.  */
  temp_npts2 = buff->param_set.num_acqs_2d;
  buff->param_set.num_acqs_2d=buff->npts2;
  make_param_string( &buff->param_set, s );
  buff->param_set.num_acqs_2d=temp_npts2;

  fprintf( fstream, s);
  fclose( fstream );

  // save proc params
  path_strcpy( fileN, path);
  path_strcat( fileN, "/proc_params" );
  fstream = fopen( fileN, "w" );

  fprintf(fstream,"PH: %f %f %f %f\n",buff->phase0_app,buff->phase1_app,
	     buff->phase20_app,buff->phase21_app);
  fprintf(fstream,"FT: %i\n",buff->flags & FT_FLAG);
  fprintf(fstream,"FT2: %i\n",buff->flags & FT_FLAG2);

  fclose( fstream );


  path_strcpy( fileN, path);
  path_strcat( fileN, "/data" );

  //printf( "creating data file: %s\n", fileN );
  fstream = fopen( fileN, "w" );
  
  fwrite( buff->data, sizeof( float ), buff->param_set.npts*buff->npts2*2, fstream ); 
  
  fclose( fstream );

  /* strip filename out of the path, and stick the path into global path */


  put_name_in_buff(buff,path);

  if (buff->buffnum ==current)
    show_parameter_frame(&buff->param_set);

  path_strcpy(buff->path_for_reload,path);
  set_window_title(buff);
    
  return 0;
}



gint pix_to_x(dbuff * buff,int xval){

  // give it a pixel value in xval, it gives you back the point number along the x axis (0 to npts-1)
  
  int npts1,npts2,i1,i2,j1,j2,ret;
  float xppt,yppt;
  
  npts1=buff->param_set.npts;
  npts2=buff->npts2;
  i1=(int) (buff->disp.xx1 * (npts1-1) +.5);
  i2=(int) (buff->disp.xx2 * (npts1-1) +.5);
  
  j1=(int) (buff->disp.yy1 * (npts2-1) +.5);
  j2=(int) (buff->disp.yy2 * (npts2-1) +.5);

  ret=0;
  if (buff->disp.dispstyle ==SLICE_ROW){
    xppt= (float) (buff->win.sizex-1.0)/(i2-i1);
    ret=(xval+xppt/2-1)/xppt+i1; 
    if (ret >= buff->param_set.npts)
      ret = buff->param_set.npts-1;
    if (ret <0) ret =0;
  }
  else if (buff->disp.dispstyle == RASTER){
      xppt= (float) buff->win.sizex/(i2-i1+1);
      ret=  (xval-1)/xppt+i1; 
    if (ret >= buff->param_set.npts)
      ret = buff->param_set.npts-1;
    if (ret <0) ret =0;

  }
  else if (buff->disp.dispstyle ==SLICE_COL){
    yppt= (float) (buff->win.sizex-1.0)/(j2-j1);
    ret= (xval+yppt/2-1)/yppt+j1;
    if (ret >= buff->npts2) ret = buff->npts2-1;
    if (ret <0) ret =0;
  }    

  return ret;
}

gint pix_to_y(dbuff * buff,int yval){

  int npts1,npts2,i1,i2,j1,j2,ret;
  float yppt;


  npts1=buff->param_set.npts;
  npts2=buff->npts2;
  i1=(int) (buff->disp.xx1 * (npts1-1) +.5);
  i2=(int) (buff->disp.xx2 * (npts1-1) +.5);

  j1=(int) (buff->disp.yy1 * (npts2-1) +.5);
  j2=(int) (buff->disp.yy2 * (npts2-1) +.5);

  if (buff->is_hyper){
    if (j1 %2 ==1) j1-=1;
    if (j2 %2 ==0) j2+=1; // different from draw_raster!!
  }

  if (j1==j2){
    if(j2>0) j1=j2-2;
    else j2=j1+2;
  }
  yval=buff->win.sizey+1-yval;

  // only get here in RASTER
  if(buff->disp.dispstyle != RASTER){
    printf("in pix_to_y, but not in RASTER mode\n");
    return 0;
  }
  yppt= (float) buff->win.sizey/(j2-j1+1);
  ret= (yval-1)/yppt+j1;
  //  printf("pix_to_y: j1, j2: %i %i, yval: %i, ret: %i\n",j1,j2,yval,ret);
  if (ret >= buff->npts2) ret =buff->npts2-1;
  if (ret <0) ret =0;
  return ret;


}


void set_window_title(dbuff *buff)
{
  char s[PATH_LENGTH];
  
 snprintf(s,PATH_LENGTH,"Buffer: %i, %s",buff->buffnum,buff->path_for_reload);
 // printf("title for window: %s\n",s);
 gtk_window_set_title(GTK_WINDOW(buff->win.window),s);
 return;
 

}
	  

void file_export(dbuff *buff,int action,GtkWidget *widget)
{

  int i1,i2,j1,j2,i,j,npts;
  double dwell2,sw2;
  char fileN[PATH_LENGTH];
  FILE *fstream;
  
  // first juggle the filename

  if (strcmp(buff->path_for_reload,"") == 0){
    popup_msg("Can't export, no reload path?");
    return;
  }

  npts = buff->param_set.npts;

  path_strcpy(fileN,buff->path_for_reload);
  path_strcat(fileN,"export.txt");
  //  printf("using filename: %s\n",fileN);
  fstream = fopen(fileN,"w");
  if ( fstream == NULL){
    popup_msg("Error opening file for export");
    return;
  }
  


  // routine exports what is viewable on screen now.

  if (buff->disp.dispstyle == SLICE_ROW){
    j= buff->disp.record*2*npts;
    i1=(int) (buff->disp.xx1 * (npts-1) +.5);
    i2=(int) (buff->disp.xx2 * (npts-1) +.5);
    if (i1==i2) {
      if (i2 >0 ) 
	i1=i2-1;
      else 
	i2=i1+1;
    }
    if (buff->flags & FT_FLAG){
      fprintf(fstream,"#point, Hz, real, imag\n");
      for(i = i1 ; i <= i2 ; i++ )
	fprintf(fstream,"%i %f %f %f\n",i, 
		-(float)buff->param_set.sw*((float) i-(float)npts/2.0)/(float)npts,
		buff->data[i*2+j],buff->data[i*2+1+j]);
    }
    else{
      fprintf(fstream,"#point, us, real, imag\n");
      for(i = i1 ; i <= i2 ; i++ )
	fprintf(fstream,"%i %f %f %f\n",i,i*buff->param_set.dwell,
	      buff->data[i*2+j],buff->data[i*2+1+j]);
    }
    
  } // not ROW
    else   
   if (buff->disp.dispstyle == SLICE_COL){
    j = 2*npts;
    i1=(int) (buff->disp.yy1 * (buff->npts2-1)+.5);
    i2=(int) (buff->disp.yy2 * (buff->npts2-1)+.5);

    if (i1==i2) {
      if (i2 >0 ) 
	i1=i2-1;
      else 
	i2=i1+1;
    }
    if (buff->is_hyper){
      if (i1 %2 ==1 ) i1-=1;
      if (i2 %2 ==1 ) i2-=1;
      fprintf(fstream,"#point, real, imag\n");
      for(i = i1 ; i <= i2 ; i+=2 )
	fprintf(fstream,"%i %f %f\n",i,
		buff->data[i*j+2* buff->disp.record2],buff->data[(i+1)*j+2* buff->disp.record2]);
    }
    else{
      fprintf(fstream,"#point, value\n");
      for(i = i1 ; i <= i2 ; i++ )
	fprintf(fstream,"%i %f\n",i,
		buff->data[i*j+2*buff->disp.record2]);
    } 
   }
  else { // must be two-d


    i1=(int) (buff->disp.xx1 * (npts-1) +.5);
    i2=(int) (buff->disp.xx2 * (npts-1) +.5);
    
    j1=(int) (buff->disp.yy1 * (buff->npts2-1)+.5);
    j2=(int) (buff->disp.yy2 * (buff->npts2-1)+.5);
    
    if (buff->is_hyper && j1 %2 == 1) j1 -= 1;
    if(buff->is_hyper && j2 %2 ==1) j2-=1;

    //get dwell in second dimension

    i = pfetch_float(&buff->param_set,"dwell2",&dwell2,0);
    if (i == 1)
      sw2 =1/dwell2;
    else
      i=  pfetch_float(&buff->param_set,"sw2",&sw2,0);
    if (i == 1)
      dwell2=1/sw2;
    else{
      
      printf("Error getting dwell2 in export_file %i\n",i);
      //fclose(fstream);
      //return;
    }


    // if its hyper complex, print only the real records 

    if (i==0) {   //2-D experiment with no dwell2...
      if ( buff->flags & FT_FLAG){
	fprintf(fstream,"#x point, Hz, y point, Hz, real, imag\n");
	for ( j=j1;j<=j2;j += 1+ buff->is_hyper ){
	  for(i=i1 ; i<=i2 ;i++)
	    fprintf(fstream,"%i %f %i %f %f\n",i,
		    -(float)buff->param_set.sw*((float)i-(float)npts/2.)/(float)npts,
		    j, buff->data[j*2*npts+2*i],buff->data[j*2*npts+2*i+1]);
	  fprintf(fstream,"\n");
	}
      } 
      else{
	fprintf( fstream, "#x point, us, y point, real, imag\n");
	for ( j=j1;j<=j2;j+=1+buff->is_hyper){
	  for(i=i1 ; i<=i2 ;i++)
	    fprintf(fstream,"%i %f %i %f %f\n",i,buff->param_set.dwell*i,
		    j, buff->data[j*2*npts+2*i],buff->data[j*2*npts+2*i+1]);
	  fprintf(fstream,"\n");
	}
      }
    }
    else {
      if ( buff->flags & FT_FLAG){
	fprintf(fstream,"#x point, Hz, y point, Hz, real, imag\n");
	for ( j=j1;j<=j2;j += 1+ buff->is_hyper ){
	  for(i=i1 ; i<=i2 ;i++)
	    fprintf(fstream,"%i %f %i %f %f %f\n",i,
		    -(float)buff->param_set.sw*((float)i-(float)npts/2.)/(float)npts,
		    j, -(((float)j)*sw2/buff->npts2-(float)sw2/2.),buff->data[j*2*npts+2*i],buff->data[j*2*npts+2*i+1]);
	  fprintf(fstream,"\n");
	}
      }
      else{
	fprintf( fstream, "#x point, us, y point, us, real, imag\n");
	for ( j=j1;j<=j2;j+=1+buff->is_hyper){
	  for(i=i1 ; i<=i2 ;i++)
	    fprintf(fstream,"%i %f %i %f %f %f\n",i,buff->param_set.dwell*i,
		    j,((float)j)*dwell2*1e6/(1+buff->is_hyper),buff->data[j*2*npts+2*i],buff->data[j*2*npts+2*i+1]);
	  fprintf(fstream,"\n");
	}	
      }
    }
	  


  }

fclose(fstream);
return;


}


void file_append(dbuff *buff,int action,GtkWidget *widget)
{
  char s[PATH_LENGTH],s2[PATH_LENGTH],s3[UTIL_LEN],old_exec[PATH_LENGTH];
  FILE *fstream;
  char params[PARAMETER_LEN];
  int npts,acq_npts,npts2;
  unsigned long sw,acqns;
  float dwell;
  double float_val,float_val2;
  int i,int_val;
  unsigned long local_ct;

  //  printf("in file_append\n");

  if (buff->npts2 != 1){
    popup_msg("Can't append a 2d data set");
    return;
  }
// first need to build a filename - same algorithm as for save
  path_strcpy(s,buff->param_set.save_path);
  path_strcat(s , "/params");

printf("append file, using path %s\n",s);

// make sure file exists
  fstream = fopen( s , "r");
  if (fstream == NULL){
    popup_msg("Couldn't open file for append");
    return;
  }

// read in the parameters


  fscanf( fstream, "%s\n", old_exec );
  fscanf( fstream, 
     "npts = %u\nacq_npts = %u\nna = %lu\nna2 = %u\nsw = %lu\ndwell = %f\n", 
	  &npts,&acq_npts,&acqns, &npts2 ,&sw,&dwell);
  //    printf("found npts: %i, current data has: %i\n",npts,buff->param_set.npts);


  if ( npts != buff->param_set.npts){
    popup_msg("Can't append to file of different npts");
    fclose(fstream);
    return;
  }
  if (strcmp ( old_exec , buff->param_set.exec_path ) != 0 )
    popup_msg("Warning: \"append\" with different pulse program");

  // read in the parameters from the file
  strcpy(params,"");

  // ct was late, see if exists

  if (fgets( s2, PATH_LENGTH, fstream) != NULL){
    if (strncmp(s2,"ct = ",5) == 0){
      sscanf(s2,"ct = %lu\n",&local_ct);
      //      printf("found ct, value = %lu\n",local_ct);
    }
    else strncat(params , s2 , PATH_LENGTH);  // if its not ct, then add the string onto p
  
  }

  local_ct += buff->ct; // add our ct to the file's


  while( fgets( s2, PATH_LENGTH, fstream ) != NULL )
    if (strlen (s2) > 1)
      strncat( params, s2, PATH_LENGTH );

  fclose( fstream );

  // now rewrite the param file, but increment na2 and fix up ct.
  fstream = fopen( s , "w");
  fprintf(fstream,"%s\n",old_exec);

  fprintf( fstream, 
     "npts = %u\nacq_npts = %u\nna = %lu\nna2 = %u\nsw = %lu\ndwell = %f\nct = %lu\n", 
	   npts, acq_npts,acqns,npts2+1 ,sw,dwell,local_ct);
  fprintf(fstream,"%s",params);

  // loop through our parameters 
  //for each, grab the one for the last record in the file.  If its
  //different from what we have currently, append it to the file. 

  if (fstream == NULL){
    popup_msg("Can't open for append");
    return;
  }

  fprintf(fstream,";\n");
  
  for (i = 0 ;i<buff->param_set.num_parameters ; i++){
    switch ( buff->param_set.parameter[i].type )
      {
      case 'i':
	sfetch_int ( params,buff->param_set.parameter[i].name,&int_val,npts2-1 );
	if (int_val != buff->param_set.parameter[i].i_val)
	  fprintf(fstream,PARAMETER_FORMAT_INT,buff->param_set.parameter[i].name,
		  buff->param_set.parameter[i].i_val);

	break;
      case 'f':
	sfetch_double( params,buff->param_set.parameter[i].name,&float_val,npts2-1);
	snprintf(s2,PATH_LENGTH,PARAMETER_FORMAT_DOUBLEP,buff->param_set.parameter[i].name,
		 buff->param_set.parameter[i].f_digits,
		 buff->param_set.parameter[i].f_val,
		buff->param_set.parameter[i].unit_s);
	sscanf(s2,PARAMETER_FORMAT_DOUBLE,s3,&float_val2);
	if (float_val != float_val2)
	  fprintf(fstream,PARAMETER_FORMAT_DOUBLEP,buff->param_set.parameter[i].name,
		  buff->param_set.parameter[i].f_digits,
		  buff->param_set.parameter[i].f_val,
		  buff->param_set.parameter[i].unit_s);

	break;
      case 't':
	sfetch_text( params,buff->param_set.parameter[i].name,s2 ,npts2-1);
	if (strcmp( s2, buff->param_set.parameter[i].t_val) != 0)
	  fprintf(fstream,PARAMETER_FORMAT_TEXT_P,buff->param_set.parameter[i].name,
		  buff->param_set.parameter[i].t_val);

	break;
      default:
	popup_msg("unknown data_type in append");

    }  

  }
  fclose(fstream);


// actually append the data.

  path_strcpy(s,buff->param_set.save_path);
  path_strcat(s , "/data");
  fstream = fopen(s,"a");
  if (fstream == NULL){
    popup_msg("Can't open data for append");
    return;
  }

  fwrite(buff->data,sizeof (float), npts*2 , fstream);
  fclose(fstream);
  return;




}


gint set_cwd(char *dir)
{
  wordexp_t word;
  // dir comes back fixed - ie expanded for any ".."'s or "~"'s in the pathname

  int result;
  
  // printf("in set_cwd with arg: %s, old working dir was %s\n",dir,getcwd(old_dir,PATH_LENGTH));
  
  result = wordexp(dir, &word, WRDE_NOCMD|WRDE_UNDEF);
  //  printf("in set_cwd: %s  dir is: %s\n",word.we_wordv[0],dir);

  result = chdir(word.we_wordv[0]);
  wordfree(&word);
  if ( result !=0 )
    perror("set_cwd1:");
    
  if ( getcwd(dir,PATH_LENGTH) == NULL)
    perror("set_cwd2: ");
	
  return result;


}

void update_param_win_title(parameter_set_t *param_set)
{
  char s[PATH_LENGTH],*s2;

  
  path_strcpy(s,param_set->save_path);
  s2 = strrchr(s,'/');
  if (s2 != NULL){
    *s2=0;
    gtk_window_set_title(GTK_WINDOW(panwindow),s);
  }
}



gint put_name_in_buff(dbuff *buff,char *fname)
{
  char dir[PATH_LENGTH],name[PATH_LENGTH],*s2;

  // fname should be fully qualified and not have a trailing /
  path_strcpy(name,"");
  path_strcpy( dir, fname);
  s2 = strrchr(dir,'/');
  if (s2 !=NULL){
    path_strcpy(name,s2+1);
    *s2 = 0;
  }
  set_cwd( dir );
  path_strcpy(buff->param_set.save_path,dir);
  path_strcat(buff->param_set.save_path,"/");
  path_strcat(buff->param_set.save_path,name);
  
  //  printf("put name in buff: %s\n",buff->param_set.save_path);
  
  if ( buff->buffnum == current )
    update_param_win_title(&buff->param_set);
  
  return 0;
  
}

void clone_from_acq(dbuff *buff, int action, GtkWidget *widget )
{

  char s[PATH_LENGTH],my_string[PATH_LENGTH];
  // gets an action of 0 if user pulls it from a menu, gets an action of 1 if its called on startup.

  if (buff->buffnum == upload_buff && action == 0 && no_acq == FALSE ) return; // if user pulled it down from the acq buffer

  if ( connected == FALSE ){
    popup_msg("Can't clone from acq - not connected to shm");
    return;
  }

  last_current = current;
  current = buff->buffnum;
  show_active_border();


  //  buff->param_set.num_parameters = 0;  moved below
  buff->param_set.num_acqs = data_shm->num_acqs;

  buff->acq_npts=data_shm->npts;

  buff->ct = data_shm->ct;
  set_ch1(buff,data_shm->ch1);
  set_ch2(buff,data_shm->ch2);

  if (buff->win.ct_label != NULL){
    snprintf(s,PATH_LENGTH,"ct: %lu",data_shm->ct);
    gtk_label_set_text(GTK_LABEL(buff->win.ct_label),s);
  }
  buff->param_set.num_acqs_2d= data_shm->num_acqs_2d;
  //  buff->param_set.dwell = data_shm->time_per_point*1e6;
  //  buff->param_set.sw = 1.0/data_shm->time_per_point;

  buff->param_set.dwell = data_shm->dwell;
  buff->param_set.sw = 1.0/data_shm->dwell*1e6;
  
  // copy path names into places, unless its acq_temp.
  
  // This is for start-up while running, copies path out of shm
  path_strcpy(s,getenv("HOME"));
  path_strcat(s,"/Xnmr/data/acq_temp");
  if (strcmp(s,data_shm->save_data_path) !=0){
    put_name_in_buff(buff,data_shm->save_data_path);
    //       	printf("special buff creation put %s in save\n",buff->param_set.save_path);
    
    
  }
  // the if {} here added Sept 28, 200<1 CM to prevent the load_param_fil
  // from loading multiple times if it doesn't have to - its
  // slow over remote connections.
  if ( strcmp(buff->param_set.exec_path,data_shm->pulse_exec_path) != 0){
    buff->param_set.num_parameters = 0;
    path_strcpy( buff->param_set.exec_path, data_shm->pulse_exec_path);
    path_strcpy( my_string, data_shm->pulse_exec_path);
    load_param_file( my_string, &buff->param_set );
  }
  //  else printf("skipping load_param_file\n");
  
  //now that we have loaded the parameters, we will set them using the fetch functions
  
  load_p_string( data_shm->parameters, data_shm->num_acqs_2d, &buff->param_set );
  


  upload_data(buff); 
  if (action == 0 ) { // means user pulled from menu.
    draw_canvas(buff);
    if (buff->buffnum == current)
      show_parameter_frame ( &buff->param_set);
  }
  
}



void set_sf1_press_event(GtkWidget *widget, GdkEventButton *event,dbuff *buff)
{
  double old_freq=0.0;
  int point,i,sf_param=-1;
  char param_search[4];
  int sf_is_float = 0,max;
  double diff;
  char s[PARAM_NAME_LEN];

  gtk_widget_destroy(setsf1dialog);
  buff->win.press_pend = 0;

  point = pix_to_x(buff, event->x);

  g_signal_handlers_unblock_by_func(G_OBJECT(buff->win.canvas),
				     G_CALLBACK (press_in_win_event),
				     buff);
  
  g_signal_handlers_disconnect_by_func (G_OBJECT (buff->win.canvas), 
				 G_CALLBACK( set_sf1_press_event), buff);
  // now in here, need to figure out what the new frequency should be, and set it.

  if (get_ch1(buff) == 'C') strncpy(param_search,"sf2",4);
  else strncpy(param_search,"sf1",4);



  // we need to know which parameter it is either way
  for (i = 0; i<buff->param_set.num_parameters ;i++){
    if (strcmp(buff->param_set.parameter[i].name, param_search) == 0){
      sf_param = i;
      i=buff->param_set.num_parameters;
    }
  }

  if (sf_param == -1){
    popup_msg("Set sf: no suitable sf parameter found\n");
    printf("no parameter with name %s found\n",param_search);
    return;
  }

  if (buff->param_set.parameter[sf_param].type == 't'){
    sscanf(buff->param_set.parameter[sf_param].t_val,"%lf",&old_freq);
  }
  else
    sf_is_float = pfetch_float(&buff->param_set,param_search,&old_freq,buff->disp.record);
  //  printf("got old_freq: %f \n",old_freq);

  if (old_freq == 0.0){
    printf("didn't get an old freq\n");
    return;
  }

  // ok, now figure out our offset from res
  diff = (point * buff->param_set.sw/buff->param_set.npts
	- buff->param_set.sw/2.);
  //  if (sf_is_float == 0) diff = diff/1e6;  freq's are always in MHz - a slightly unfortunate historical choice...
  diff /= 1e6;
  old_freq -= diff;

  if (sf_is_float == 1) {
     
    if (buff->param_set.parameter[sf_param].type == 'F'){ // unarray it
      printf("unarraying\n");
      g_free(buff->param_set.parameter[sf_param].f_val_2d);
      buff->param_set.parameter[sf_param].f_val_2d = NULL;
      buff->param_set.parameter[sf_param].type = 'f';
      buff->param_set.parameter[sf_param].size = 0;  

      if (buff->buffnum == current ){ // only need to fix up the label if we're current
	strncpy( s, buff->param_set.parameter[sf_param].name,PARAM_NAME_LEN); 
	strncat( s, "(",1 ); 
	strncat( s, &buff->param_set.parameter[sf_param].unit_c,1 ); 
	strncat( s, ")",1 ); 
	gtk_label_set_text( GTK_LABEL( param_button[ sf_param ].label ), s ); 
      }
      // we unarrayed, now set acq_2d to biggest...
      max=1;
      for (i=0;i<buff->param_set.num_parameters;i++){
	if (buff->param_set.parameter[i].size > max) 
	  max = buff->param_set.parameter[i].size;
      }
      //      printf("unarray in set_sf1_press_event: max size was %i\n",max);
      if (buff->buffnum == current) 
	gtk_adjustment_set_value( GTK_ADJUSTMENT( acqs_2d_adj ), max ); 
      else buff->param_set.num_acqs_2d = max;      
    } // that was the end of unarraying

    if (buff->buffnum == current)
      gtk_adjustment_set_value(GTK_ADJUSTMENT(param_button[sf_param].adj),old_freq/buff->param_set.parameter[sf_param].unit);    
    else
      buff->param_set.parameter[sf_param].f_val = old_freq/buff->param_set.parameter[sf_param].unit;
  }
  else{
    snprintf(s,PARAM_T_VAL_LEN,"%.7f",old_freq);
    if (buff->buffnum == current)
      gtk_entry_set_text(GTK_ENTRY(param_button[sf_param].ent),s);

    // apparently we need to do this either way:
    snprintf(buff->param_set.parameter[sf_param].t_val,PARAM_T_VAL_LEN,"%.7f",old_freq);

  }
  //  printf("setting freq to: %s\n",buff->param_set.parameter[sf_param].t_val);
  
  // hmm, don't know how to set the adjustment, so instead, put this value into the parameter

  // then need to reshow parameter frame - this was the slow call - done better now above.
    //  show_parameter_frame ( &buff->param_set); // this shouldn't be here.  should just fix up the one param as unarray does...

}


void set_sf1(dbuff *buff, int action, GtkWidget *widget )
{

  GtkWidget * setsf1label;

  if (buff->win.press_pend > 0){
    popup_msg("Can't start set_sf1 while press pending");
    return;
  }

  if (buff->buffnum == upload_buff && acq_in_progress == ACQ_RUNNING){
    popup_msg("Can't change frequency while acq is running");
    return;
  }

  if ((buff->flags & FT_FLAG) == 0){
    popup_msg("Can't set frequency from the time domain");
    return;
  }

  buff->win.press_pend=1;

  // block the ordinary press event
  g_signal_handlers_block_by_func(G_OBJECT(buff->win.canvas),
				     G_CALLBACK (press_in_win_event),
				     buff);

  // connect our event
  g_signal_connect (G_OBJECT (buff->win.canvas), "button_press_event",
                        G_CALLBACK( set_sf1_press_event), buff);
  

  // raise out window and give some instruction

  last_current = current;
  current = buff->buffnum;
  show_active_border();

  setsf1dialog = gtk_dialog_new(); 
  setsf1label = gtk_label_new("Click on the new carrier frequency");
  gtk_container_set_border_width( GTK_CONTAINER(setsf1dialog), 5 ); 
  gtk_box_pack_start ( GTK_BOX( (GTK_DIALOG(setsf1dialog)->vbox) ), setsf1label, FALSE, FALSE, 5 ); 


  gtk_window_set_transient_for(GTK_WINDOW(setsf1dialog),GTK_WINDOW(panwindow));
  gtk_window_set_position(GTK_WINDOW(setsf1dialog),GTK_WIN_POS_CENTER_ON_PARENT);

  gtk_widget_show_all (setsf1dialog); 




  return;

}

void reset_dsp_and_synth(dbuff *buff, int action, GtkWidget *widget){

  if (no_acq != FALSE)
    popup_msg("Can't reset dsp and synth in noacq mode");
  else if (acq_in_progress != ACQ_STOPPED)
    popup_msg("Can't reset dsp and synth while running");
  else{
    data_shm->reset_dsp_and_synth = 1;
    popup_msg("DSP and Synth will be reset on start of next acq");
  }
  
}
void calc_rms(dbuff *buff, int action, GtkWidget *widget )
{

  int i,j;

  float sum=0,sum2=0,avg,avgi,rms,rmsi,sumi=0,sum2i=0;
  char out_string[UTIL_LEN];

 for (j = 0; j<buff->npts2;j+=buff->is_hyper+1){      //do each slice and output all to screen
   sum=0.;
   sum2=0.;
   sumi=0.;
   sum2i=0;
   avg=0.;
   avgi=0.;
   rms=0.;
   rmsi=0.;

  //  printf("in calc_rms\n");
  for (i=0;i<buff->param_set.npts;i++){
   sum  += buff->data[buff->param_set.npts*2*j+2*i]  ;
   sumi += buff->data[buff->param_set.npts*2*j+2*i+1];

   sum2 += buff->data[buff->param_set.npts*2*j+2*i]  *buff->data[buff->param_set.npts*2*j+2*i]  ;
   sum2i+= buff->data[buff->param_set.npts*2*j+2*i+1]*buff->data[buff->param_set.npts*2*j+2*i+1];
 }


 
 avg = sum /buff->param_set.npts;
 avgi = sumi /buff->param_set.npts;

 rms = sqrt(sum2/buff->param_set.npts - avg*avg);
 rmsi = sqrt(sum2i/buff->param_set.npts - avgi*avgi);
 
 printf("RMS: Real: %f  Imaginary: %f\n",rms, rmsi);
 if (j==buff->disp.record){
   snprintf(out_string,UTIL_LEN,"RMS for real: %f, imag: %f",rms,rmsi);
   popup_msg(out_string);
 }
 }
 printf("\n");
 draw_canvas(buff);
  return;

}

void set_ch1(dbuff *buff,char ch1){
  if (ch1 == 'A') gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(buff->win.but1a),TRUE);
  else if (ch1 == 'B') gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(buff->win.but1b),TRUE);
  else if (ch1 == 'C') gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(buff->win.but1c),TRUE);
}
void set_ch2(dbuff *buff,char ch2){
  if (ch2 == 'A') gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(buff->win.but2a),TRUE);
  else if (ch2 == 'B') gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(buff->win.but2b),TRUE);
  else if (ch2 == 'C') gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(buff->win.but2c),TRUE);
}

char get_ch1(dbuff *buff){
  if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(buff->win.but1a))) return 'A';
  if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(buff->win.but1b))) return 'B';
  if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(buff->win.but1c))) return 'C';

  printf("in get_ch1, couldn't find a channel!!!\n");
  exit(0);
  return 'A';
}

char get_ch2(dbuff *buff){
  if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(buff->win.but2a))) return 'A';
  if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(buff->win.but2b))) return 'B';
  if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(buff->win.but2c))) return 'C';

  printf("in get_ch2, couldn't find a channel!!!\n");
  exit(0);
  return 'C';

}
void bug_found(){
  popup_msg("It appears as though some action performed in the past second or two\nhas caused an internal inconsistency\nPlease note what actions were being taken. The user interface program is likely unstable\nThere should be other error windows with some more information");
}

void check_for_overrun_timeout(gpointer data){
  int i;
  int count=0;
  char title[UTIL_LEN*2];
  static char bug=0;
  //  printf("in check for overrun_timeout\n"); 
  if (bug == 1) return;  // already reported.

  gdk_threads_enter();
  for (i=0;i<MAX_BUFFERS;i++){
    if (buffp[i] != NULL){
      count +=1;

      if (buffp[i]->buffnum != i){
	snprintf(title,UTIL_LEN,"error, buffnum doesn't match for buff: %i",i);
	popup_msg(title);
	bug=1;
      }
	
      if (buffp[i]->overrun1 != 85*(1+256+65536+16777216)){
	printf("buffer %i overrun1 wrong!!!\n",i);
	popup_msg("found an overrun1 problem!");
	bug = 1;
      }
      if (buffp[i]->overrun2 != 85*(1+256+65536+16777216)){
	printf("buffer %i overrun2 wrong!!!\n",i);
	popup_msg("found an overrun2 problem!");
	bug = 1;
      }
    }
  }
  if (count != num_buffs){
    popup_msg("check overrun, wrong number of buffers");
    
  } 
  if (bug == 1) bug_found();
  gdk_threads_leave();
}

gint channel_button_change(GtkWidget *widget,dbuff *buff){

  static char norecur = 0;


  if (!GTK_TOGGLE_BUTTON (widget)->active) {
    printf("channel button not active\n");
    return TRUE;
  }

// the norecur things shouldn't be necessary - we only get here on the pressed signal.
  if (norecur != 0){
    //    printf("got norecur\n");
    norecur = 0;
    return TRUE;
  }
  

  //    printf("in channel_button_change, buffnum: %i, acq_in_progress %i\n",buff->buffnum,acq_in_progress);

  if (buff->buffnum == upload_buff && acq_in_progress != ACQ_STOPPED){    
    if ( no_update_open == 0)
      popup_no_update("Can't change channels while acq is in progress");
    if (widget == buff->win.but1a || widget == buff->win.but1b || widget == buff->win.but1c){
      //      printf("got channel 1 setting to: %c\n",data_shm->ch1);
      norecur = 1;
      set_ch1(buff,data_shm->ch1);
    }
    else  if (widget == buff->win.but2a || widget == buff->win.but2b || widget == buff->win.but2c){
      //      printf("got channel 2\n");
      norecur = 1;
      set_ch2(buff,data_shm->ch2);
    }
    else printf("channel button change: got unknown widget\n");
    return FALSE;
  }
  return FALSE;

}



void cswap(float *points,int i1, int i2){
  float temp;
  temp=points[i1];
  points[i1] = points[i2];
  points[i2] = temp;
}

void csort(float *points,int num){
  int i,j;

  if (num < 2) return;

  for(i = 1; i < num ; i++ ){
    if (points[i] < points[i-1]){
      // swap them:
      cswap(points,i,i-1);
      // then keep looking
      for (j = i-1;j>0;j--)
	if(points[j]<points[j-1])
	  cswap(points,j,j-1);
    }

  }
  //  for(i=0;i<num;i++)
  //    printf("%f\n",points[i]);
  
}
  
void calc_spline_fit(dbuff *buff,float *spline_points, float *yvals, int num_spline_points,
		     float *y2vals, float *temp_data){

  int i;
  float x,y;

  for (i=0;i<num_spline_points;i++){
    yvals[i]=buff->data[ 2*((int) (spline_points[i]*(buff->param_set.npts-1)+0.5) ) 
		       +buff->disp.record*buff->param_set.npts*2];
  }
  spline(spline_points-1,yvals-1,num_spline_points,0.,0.,y2vals-1);

  for(i=0;i<buff->param_set.npts;i++){
    x = ((float) i)/(buff->param_set.npts-1);
    splint(spline_points-1,yvals-1,y2vals-1,num_spline_points,x,&y);
    
    temp_data[2*i] = y;
    //    printf("%f %f\n",x,y);
  }


}



void baseline_spline(dbuff *buff, int action, GtkWidget *widget)
{
  static int num_spline_points=0;
  static float spline_points[100];
  static GtkWidget* dialog=NULL;
  GtkWidget* button;
  GtkWidget* label;
  static int current_buff = -1;
  static int redo_buff = -1;
  GdkEventButton *event;
  int i,j;
  float xval,x,y;
  char old_point=0;
  float * temp_data;
  static float yvals[100],y2vals[100];
  static float *undo_buff=NULL;
  static int undo_num_points=0;

  /* this routine is a callback for the spline menu items.
     It is also a callback for responses internally.  In that case, the buff that comes in won't be right... */

  //  printf("buff: %i, action: %i, widget %i, dialog: %i\n",(int) buff, action, (int) widget, (int) dialog);


  /* First:  if we get a point */
  if (current_buff != -1){

    if ((void *) buff == (void *) buffp[current_buff]->win.canvas){
      event = (GdkEventButton *) action;

      if (buffp[current_buff]->disp.dispstyle==SLICE_ROW){
	xval= (event->x-1.)/(buffp[current_buff]->win.sizex-1) *
	  (buffp[current_buff]->disp.xx2-buffp[current_buff]->disp.xx1)
	  +buffp[current_buff]->disp.xx1;

	//	printf("got x val: %f\n",xval);

	// check to see if this point already exists.  If so, erase it.	
	for(i=0;i<num_spline_points;i++)
	  // need to map the frac's that are stored into pixels...
	  // otherwise you can't erase them properly.
	  if (event->x == (int) ((spline_points[i]-buffp[current_buff]->disp.xx1)*
	    (buffp[current_buff]->win.sizex-1)/
	    (buffp[current_buff]->disp.xx2-buffp[current_buff]->disp.xx1)+1.5)){


	    old_point = 1;
	    printf("found old point, erase it\n");
	    draw_vertical(buffp[current_buff],&colours[WHITE],0.,(int)event->x);
	    for ( j = i ; j < num_spline_points-1 ; j++ )
	      spline_points[j] = spline_points[j+1];
	    num_spline_points -= 1;
	  }
	if (old_point == 0){
	  if(num_spline_points < 100){
	    spline_points[num_spline_points] = xval;
	    num_spline_points +=1;
	    draw_vertical(buffp[current_buff],&colours[BLUE],0.,(int)event->x);	    
	  }
	  else {
	    popup_msg("Too many spline points!");
	    return;
	  }
	}


      }
      else{
	popup_msg("To finish picking points, must view a row");
	return;
      }


      return;
    } // end of press event
    


    /* This mean's we're done collecting points */
    
    if ((void *) buff == (void *) dialog){ 
      //           printf("buff is dialog!\n");
      gtk_object_destroy(GTK_OBJECT(dialog));
      
      if ( buffp[current_buff] == NULL){ // our buffer destroyed while we were open...
	printf("Baseline_spline: buffer was destroyed while we were open\n");
      }
      else{
	buffp[current_buff]->win.press_pend = 0;
	g_signal_handlers_disconnect_by_func (G_OBJECT (buffp[current_buff]->win.canvas), 
					      G_CALLBACK( baseline_spline), buffp[current_buff]);
	
	g_signal_handlers_unblock_by_func(G_OBJECT(buffp[current_buff]->win.canvas),
					  G_CALLBACK (press_in_win_event),
					  buffp[current_buff]);
      }
      
      //      printf("got a total of: %i points for spline\n",num_spline_points);
      draw_canvas(buffp[current_buff]);
      current_buff = -1;

      // the spline points should be in order:
      csort(spline_points,num_spline_points);


      return;
    }
    
  } // closes "whether or not this is a new open"
  else{
    if (action == PICK_SPLINE_POINTS){
      
      if (buff->win.press_pend != 0){
	popup_msg("There's already a press pending\n(maybe Expand or Offset?)");
	return;
      }

      if (buff->disp.dispstyle != SLICE_ROW){
	popup_msg("Spline only works on rows for now");
	return;
      }

      // first, want to draw in the old points.
      //      num_spline_points = 0;

      for(i=0;i<num_spline_points;i++){
	    draw_vertical(buff,&colours[BLUE],spline_points[i],-1);	    
      }

      
      
      /* open up a window that we click ok in when we're done */
      dialog = gtk_dialog_new();
      //    gtk_window_set_modal( GTK_WINDOW( dialog ), TRUE );
      label = gtk_label_new ( "Hit ok when baseline points have been entered" );
      button = gtk_button_new_with_label("OK");
      
      /* catches the ok button */
      g_signal_connect_swapped(G_OBJECT (button), "clicked", G_CALLBACK (baseline_spline), G_OBJECT( dialog ) );
      
      /* also need to catch when we get a close signal from the wm */
      g_signal_connect(G_OBJECT (dialog),"delete_event",G_CALLBACK (baseline_spline),G_OBJECT( dialog ));
      
      gtk_box_pack_start (GTK_BOX ( GTK_DIALOG(dialog)->action_area ),button,FALSE,FALSE,0);
      gtk_container_set_border_width( GTK_CONTAINER(dialog), 5 );
      gtk_box_pack_start ( GTK_BOX( (GTK_DIALOG(dialog)->vbox) ), label, FALSE, FALSE, 5 );
      
      gtk_window_set_transient_for(GTK_WINDOW(dialog),GTK_WINDOW(panwindow));
      gtk_window_set_position(GTK_WINDOW(dialog),GTK_WIN_POS_CENTER_ON_PARENT);
      
      gtk_widget_show_all (dialog);
      
      current_buff = buff->buffnum;
      g_signal_handlers_block_by_func(G_OBJECT(buff->win.canvas),
				      G_CALLBACK (press_in_win_event),
				      buff);
      // connect our event
      g_signal_connect (G_OBJECT (buff->win.canvas), "button_press_event",
                        G_CALLBACK( baseline_spline), buff);
      buff->win.press_pend=1;
      
  
      //      printf("pick spline points\n");
    }
    else if (action == DO_SPLINE){
      //      printf("do spline\n");

      if (buff->disp.dispstyle != SLICE_ROW){
	popup_msg("Spline only works on rows for now");
	return;
      }


      if (num_spline_points < 3){
	popup_msg("Need at least 3 points for spline");
	return;
      }
      
      	// set up for undo.

      if (buff->param_set.npts != undo_num_points){
	if (undo_buff != NULL) 
	  g_free(undo_buff);
	undo_buff = g_malloc(buff->param_set.npts*8);
	undo_num_points = buff->param_set.npts;
      }
      for(i=0;i<undo_num_points*2;i++)
	undo_buff[i] = buff->data[i];
      redo_buff = buff->buffnum;
      
      // first need to build our array.
      for (i = 0 ; i < num_spline_points ; i++ ){
	yvals[i]=buff->data[ 2*((int) (spline_points[i]*(buff->param_set.npts-1)+0.5) ) 
			   +buff->disp.record*buff->param_set.npts*2];
	//	printf("%f %f\n",spline_points[i],yvals[i]);
      }
      spline(spline_points-1,yvals-1,num_spline_points,0.,0.,y2vals-1);
      
      /* now apply the spline to the data */
      for(i=0;i<buff->param_set.npts;i++){
	x = ((float) i)/(buff->param_set.npts-1);
	splint(spline_points-1,yvals-1,y2vals-1,num_spline_points,x,&y);
	
	  buff->data[2*i+buff->disp.record*buff->param_set.npts*2] -= y;
	  //printf("%i %f\n",i, y);
      }
      //      printf("got all the points done, gonna redraw\n");
      // to do the imaginary part, play some tricks to generate it...
      // first, make sure npts is a power of 2.
      if  (log(buff->param_set.npts)/log(2.) == rint(log(buff->param_set.npts)/log(2.))){
	temp_data = g_malloc(2*8*buff->param_set.npts);
	for(i=0;i<buff->param_set.npts;i++){
	  // copy data to temp buffer that's twice as long.
	  temp_data[2*i]=buff->data[2*i+buff->disp.record*2*buff->param_set.npts];
	  temp_data[2*i+1]=0.;
	  temp_data[2*i+buff->param_set.npts*2]=0.;
	  temp_data[2*i+1+buff->param_set.npts*2]=0.;
	}
	four1(temp_data-1,buff->param_set.npts*2,1);
	for(i=0;i<buff->param_set.npts*2;i++){
	  temp_data[i+buff->param_set.npts*2]=0; // set the second half to 0
	}
	// and do the reverse ft.
	four1(temp_data-1,buff->param_set.npts*2,-1);
	for(i=0;i<buff->param_set.npts*2;i++){
	  buff->data[i+buff->disp.record*2*buff->param_set.npts]=temp_data[i]/buff->param_set.npts;
	}
	g_free(temp_data);

	
      } // if not, give up on imag part...
      else printf("Fixing up the imaginary part failed - npts not a power of 2\n");

      draw_canvas(buff);
    }

    
    else if(action == SHOW_SPLINE_FIT){
      //      printf("show_spline_fit\n");
      
      if (buff->disp.dispstyle != SLICE_ROW){
	popup_msg("Spline only works on rows for now");
	return;
      }

      temp_data = g_malloc(8*buff->param_set.npts);
      calc_spline_fit(buff,spline_points,yvals,num_spline_points,y2vals,temp_data);
      
      draw_row_trace(buff, 0.,0.,temp_data,buff->param_set.npts, &colours[BLUE],0);
      gtk_widget_queue_draw_area(buff->win.canvas,1,1,buff->win.sizex,buff->win.sizey);
      
      g_free(temp_data);
      



    } // end of show_spline_fit
    else if(action == CLEAR_SPLINE_POINTS){
      num_spline_points = 0;
    }
    else if(action == UNDO_SPLINE){
      if ( redo_buff == buff->buffnum && undo_buff != NULL && 
	   undo_num_points == buff->param_set.npts){
	// we're good to go.
	for(i=0;i<undo_num_points*2;i++)
	  buff->data[i] = undo_buff[i];
	draw_canvas(buff);
      }
      else{
	popup_msg("undo spline not available");
	return;
      }

    }

    else printf("baseline_spline got unknown action: %i\n",action);
  }
}


/* for measuring timing:
 struct timeval t1,t2;
 struct timezone tz;
 int tt;
 gettimeofday(&t1,&tz);



 gettimeofday(&t2,&tz);
 tt = (t2.tv_sec-t1.tv_sec)*1e6 + t2.tv_usec-t1.tv_usec;
 printf("took: %i usec\n",tt);
*/



void add_subtract(dbuff *buff, int action, GtkWidget *widget ){

  // need to maintain lists of buffers - should probably do it at buffer creation and kill time.

  


  if (add_sub.shown == 0){
    gtk_widget_show_all(add_sub.dialog);
    add_sub.shown = 1;
  }
  else
    gdk_window_raise( add_sub.dialog->window);

  

  printf("in add_subtract stub\n");
}

void add_sub_changed(GtkWidget *widget,gpointer data){


  int i,j,k;
  double f1,f2;
  int sbnum1,sbnum2,dbnum;
  char s[5];

  i= gtk_combo_box_get_active(GTK_COMBO_BOX(add_sub.s_buff1));
  j= gtk_combo_box_get_active(GTK_COMBO_BOX(add_sub.s_buff2));
  k= gtk_combo_box_get_active(GTK_COMBO_BOX(add_sub.dest_buff));

  if (i == -1 || j == -1 || k == -1) return; // got no selection can happen during program shutdown 

  sbnum1 = add_sub.index[i];
  sbnum2 = add_sub.index[j];
  if (k==0) dbnum = -1;
  else dbnum = add_sub.index[k-1];
  

  //  printf("buffers: %i %i ",add_sub.index[i],add_sub.index[j]);
  //  if (k==0) printf("new\n");
  //  else
  //    printf("%i\n",add_sub.index[k-1]);

  i= gtk_combo_box_get_active(GTK_COMBO_BOX(add_sub.s_record1));
  j= gtk_combo_box_get_active(GTK_COMBO_BOX(add_sub.s_record2));
  k= gtk_combo_box_get_active(GTK_COMBO_BOX(add_sub.dest_record));

  //  printf("got actives: %i %i %i\n",i,j,k);

  f1 = gtk_spin_button_get_value(GTK_SPIN_BUTTON(add_sub.mult1));
  f2 = gtk_spin_button_get_value(GTK_SPIN_BUTTON(add_sub.mult2));
  
  //  printf(" got multipliers: %lf %lf\n",f1,f2);

  


  if (widget == add_sub.s_buff1){
    // first buffer changed, fix the number of records:
    if (buffp[sbnum1]->npts2 < add_sub.s_rec_c1){ // too many
      for (i= add_sub.s_rec_c1-1; i >= buffp[sbnum1]->npts2;i--){
	//	printf("deleting record: %i\n",i);
	gtk_combo_box_remove_text(GTK_COMBO_BOX(add_sub.s_record1),i+2);
      }
    }
    else if(buffp[sbnum1]->npts2 > add_sub.s_rec_c1){ // too few
      for (i=add_sub.s_rec_c1;i<buffp[sbnum1]->npts2;i++){
	sprintf(s,"%i",i);
	//	printf("adding record: %i\n",i);
	gtk_combo_box_append_text(GTK_COMBO_BOX(add_sub.s_record1),s);
      }
    }
    add_sub.s_rec_c1 = buffp[sbnum1]->npts2;
  }

  else if (widget == add_sub.s_buff2){
    // second buffer changed, fix the number of records
    if (buffp[sbnum2]->npts2 < add_sub.s_rec_c2){// too many
      for (i= add_sub.s_rec_c2-1; i >= buffp[sbnum2]->npts2;i--){
	//	printf("deleting record: %i\n",i);
	gtk_combo_box_remove_text(GTK_COMBO_BOX(add_sub.s_record2),i+2);
      }
    }
    else if(buffp[sbnum2]->npts2 > add_sub.s_rec_c2){ // too few
      for (i=add_sub.s_rec_c2;i<buffp[sbnum2]->npts2;i++){
	sprintf(s,"%i",i);
	//	printf("adding record: %i\n",i);
	gtk_combo_box_append_text(GTK_COMBO_BOX(add_sub.s_record2),s);
      }
    }
    add_sub.s_rec_c2 = buffp[sbnum2]->npts2;

  }

  else if (widget == add_sub.dest_buff){
    // dest buff changed, fix number of records
    int new_num = 1; // if its to a 'new' buffer, assume just one record in it.
    if (dbnum >= 0) new_num = buffp[dbnum]->npts2;
    if (new_num < add_sub.dest_rec_c){
      for (i= add_sub.dest_rec_c-1; i >= new_num;i--){ // too many
	//	printf("deleting record: %i\n",i);
	gtk_combo_box_remove_text(GTK_COMBO_BOX(add_sub.dest_record),i+2);
      }
    }
    else if(new_num > add_sub.dest_rec_c){ // too few
      for (i=add_sub.dest_rec_c;i<new_num;i++){
	sprintf(s,"%i",i);
	//	printf("adding record: %i\n",i);
	gtk_combo_box_append_text(GTK_COMBO_BOX(add_sub.dest_record),s);
      }
    }
    add_sub.dest_rec_c = new_num;
  }

  
  else if (widget == add_sub.s_record1){ // source 1 record # changed
    // if one source entry goes to each then all source entries go to each.
    if (gtk_combo_box_get_active(GTK_COMBO_BOX(add_sub.s_record1)) == 0)
      gtk_combo_box_set_active(GTK_COMBO_BOX(add_sub.dest_record),0);
  }

  else if (widget == add_sub.s_record2){ // source 2 record # changed
    if (gtk_combo_box_get_active(GTK_COMBO_BOX(add_sub.s_record2)) == 0)
      gtk_combo_box_set_active(GTK_COMBO_BOX(add_sub.dest_record),0);
  }

  else if (widget == add_sub.dest_record){
    //    printf("dest record changed\n");
  }

  



}
void add_sub_buttons(GtkWidget *widget,gpointer data){
  int my_current;

  if (widget == add_sub.close){ // close button
    gtk_widget_hide(add_sub.dialog);
    add_sub.shown = 0;
  }


  else if (widget == add_sub.apply){

    float *temp_data1=NULL,*temp_data2=NULL,*data1,*data2;
    int i,j,k,l,npts,dest_rec;
    double f1,f2;
    //    printf("in add_sub_changed stuff, got data: %i\n",(int) data);
    int sbnum1,sbnum2,dbnum;


  i= gtk_combo_box_get_active(GTK_COMBO_BOX(add_sub.s_buff1));
  j= gtk_combo_box_get_active(GTK_COMBO_BOX(add_sub.s_buff2));
  k= gtk_combo_box_get_active(GTK_COMBO_BOX(add_sub.dest_buff));


  // check that there are valid selections
  if (i>=0)  sbnum1 = add_sub.index[i];
  else{
    popup_msg("Invalid source buffer 1");
    return;
  }

  if (j>=0) sbnum2 = add_sub.index[j];
  else{
    popup_msg("Invalid source buffer 2");
    return;
  }
  if (k==0) dbnum = -1;
  else if (k>0) dbnum = add_sub.index[k-1];
  else{
    popup_msg("Invalid destination buffer");
    return;
  }
    

  i= gtk_combo_box_get_active(GTK_COMBO_BOX(add_sub.s_record1));
  j= gtk_combo_box_get_active(GTK_COMBO_BOX(add_sub.s_record2));
  k= gtk_combo_box_get_active(GTK_COMBO_BOX(add_sub.dest_record));

  if (i<0 || j<0||k<0){
    popup_msg("Missing record selection");
    return;
  }

  // make sure the records exist - they may not!
  if (i> 1 && i-2 >= buffp[sbnum1]->npts2){
    popup_msg("Invalid record for source 1");
    return;
  }
  if (j>1 && j-2 >= buffp[sbnum2]->npts2){
    popup_msg("Invalid record for source 1");
    return;
  }
  if (dbnum >=0){
    if (k>1 && k-2 >= buffp[dbnum]->npts2){
      popup_msg("Invalid record for source 1");
      return;
    }
  }

  if(buffp[sbnum1]->param_set.npts != buffp[sbnum2]->param_set.npts){
    popup_msg("Inputs must have same number of points!");
    return;
  }
  npts = buffp[sbnum1]->param_set.npts;


  // grab the multiplers
  f1 = gtk_spin_button_get_value(GTK_SPIN_BUTTON(add_sub.mult1));
  f2 = gtk_spin_button_get_value(GTK_SPIN_BUTTON(add_sub.mult2));

  // ok, now just need to make sure that the destination makes sense and do it.
  /* options here:

  each each     each 
  each all      each 
  each #        each 
  all  all      # or append 
  all  #        # or append
  #    #        # or append 

on dest, any # can be replaced with append.
and all dests can be replaced with new. 

if the number of records on the input records doesn't match for "each each", error out. */

  // last error checking before we go:
  if (i==0 || j == 0){ // there's at least one each
    if (k != 0){
      popup_msg("output must be each in an input is each ");
      return;
    }
    if (i==0 && j == 0) if (buffp[sbnum1]->npts2 !=buffp[sbnum2]->npts2){
      popup_msg("inputs must have same number of records");
      return;
    }
  }

  // if there's no each on the input, then can't have each on output
  if (i != 0 && j != 0 && k == 0){
    popup_msg("can't have output each if inputs specify records");
    return;
  }

  // inputs can't overlap with output can't overlap
  if (dbnum == sbnum1 || dbnum == sbnum2){
    popup_msg("output buffer can't be an input buffer");
    return;
  }

  // make sure dest buffer isn't busy acquiring.
  if (dbnum == upload_buff && acq_in_progress != ACQ_STOPPED){
    popup_msg("output buffer is busy acquiring");
    return;
  }



  // create a new buffer if we need to
  if (dbnum == -1){
    my_current = current;
    file_new(NULL,0,NULL);
    dbnum = current;
    if (dbnum == my_current) // didn't create buffer, too many?
      return;
    gtk_combo_box_set_active(GTK_COMBO_BOX(add_sub.dest_buff),num_buffs);
    if (k == 1){
      k = 2; // if it would have been append, set to first record.
      gtk_combo_box_set_active(GTK_COMBO_BOX(add_sub.dest_record),2);
    }

  }





  // do the each's first:
  if (i==0 || j == 0){ // there's at least one each
    
    // looks like we're good to go for at least one each.  prepare the output buffer:

    if (i == 0) // if there's only 1 each, how many records?
      my_current = sbnum1; // temporary abuse of my_current
    else my_current = sbnum2;

    buff_resize(buffp[dbnum],npts,buffp[my_current]->npts2);

    // now, is it each + each, each + sum all or each + record ?

    if (i == 0 && j == 0){ // each + each:
      for (l=0;l<buffp[sbnum1]->npts2;l++)
	for(k=0;k<buffp[sbnum1]->param_set.npts*2;k++)
	  buffp[dbnum]->data[k + l* npts*2] = 
	    f1 * buffp[sbnum1]->data[k + l* npts*2] +
	    f2 * buffp[sbnum2]->data[k + l* npts*2];
      printf("did each + each -> each\n");
    }
    else if (i == 1 || j == 1){ // one is a sum all
      float *temp_data;
      temp_data = g_malloc(npts*8);
      memset(temp_data,0,npts*8);
      if (i == 1){// first is a sum all
	// collapse the sum all into temp:
	for (k=0;k<npts*2;k++)
	  for(l=0;l<buffp[sbnum1]->npts2;l++)
	    temp_data[k] += buffp[sbnum1]->data[k+l*npts*2];
	// then do it:
	for (k=0;k<npts*2;k++)
	  for(l=0;l<buffp[sbnum2]->npts2;l++)
	    buffp[dbnum]->data[k + l*npts*2] = 
	      f1* temp_data[k] + f2* buffp[sbnum2]->data[k + l* npts*2];
	g_free(temp_data);
	printf("did each + sum_all\n");
      }
      else{ // j == 1 so second is sum all
	// collapse the sum all into temp:
	for (k=0;k<npts*2;k++)
	  for(l=0;l<buffp[sbnum2]->npts2;l++)
	    temp_data[k] += buffp[sbnum2]->data[k+l*npts*2];
	// then do it:
	for (k=0;k<npts*2;k++)
	  for(l=0;l<buffp[sbnum1]->npts2;l++)
	    buffp[dbnum]->data[k + l*npts*2] = 
	      f2* temp_data[k] + f1* buffp[sbnum1]->data[k + l* npts*2];
	g_free(temp_data);
	printf("did  sum_all + each\n");
      }
    }
    else if (i > 1){
      for (k=0;k<npts*2;k++)
	for(l=0;l<buffp[sbnum2]->npts2;l++)
	  buffp[dbnum]->data[k + l*npts*2] = 
	    f1* buffp[sbnum1]->data[k+(i-2)*npts*2] + f2* buffp[sbnum2]->data[k + l* npts*2];

      printf("did  record + each");
    }
    else{
      for (k=0;k<npts*2;k++)
	for(l=0;l<buffp[sbnum1]->npts2;l++)
	  buffp[dbnum]->data[k + l*npts*2] = 
	    f1* buffp[sbnum1]->data[k+l*npts*2] + f2* buffp[sbnum2]->data[k + (j-2)* npts*2];
      printf("did each + record\n");
    }
  }
  else{    // neither is an each - output is a single record
  
    if ( k == 0 ) printf("got dest each, can't happen here\n");
    if (k == 1){ // deal with append
      dest_rec = buffp[dbnum]->npts2;
      buff_resize(buffp[dbnum],npts,buffp[dbnum]->npts2+1); // make sure npts is right
    }
    else{ // simple record assignment of output.
      buff_resize(buffp[dbnum],npts,buffp[dbnum]->npts2); // make sure npts is right
      dest_rec = k-2;
    }
    
    if (i==1){
      printf("first source is sum all\n");
      temp_data1 = g_malloc(npts*8);
      memset(temp_data1,0,npts*8);
      for (k=0;k<npts*2;k++)
	for(l=0;l<buffp[sbnum1]->npts2;l++)
	  temp_data1[k]+=buffp[sbnum1]->data[k+l*npts*2];
      data1=temp_data1;
    }
    else data1 = &buffp[sbnum1]->data[(i-2)*npts*2];
    if (j==1){
      printf("second source is sum all\n");
      temp_data2 = g_malloc(npts*8);
      memset(temp_data2,0,npts*8);
      for (k=0;k<npts*2;k++)
	for(l=0;l<buffp[sbnum2]->npts2;l++)
	  temp_data2[k]+=buffp[sbnum2]->data[k+l*npts*2];
      data2=temp_data2;
    }
    else data2=&buffp[sbnum2]->data[(j-2)*npts*2];
    for (k=0;k<npts*2;k++)
      buffp[dbnum]->data[k+dest_rec*npts*2] = f1 * data1[k] + f2 * data2[k];
    printf("did two simple adds\n");
    if (i == 1) g_free(temp_data1);
    if (j == 1) g_free(temp_data2);
  }
  draw_canvas(buffp[dbnum]);


  } // end apply button



  else printf("in add_sub_button, got an unknown button?\n");

} // end add_buttons







gint hide_add_sub(GtkWidget *widget,gpointer data){
  add_sub_buttons(add_sub.close,NULL);
  return TRUE;

}

/* useless comments */
