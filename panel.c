#define GTK_DISABLE_DEPRECATED
/* panel.c
 *
 * source code for Xnmr panel
 * Part of the Xnmr software project
 *
 * UBC Physics
 * April, 2000
 * 
 * written by: Scott Nelson, Carl Michal
 */


#include "panel.h"
#include "process_f.h"
#include "param_f.h"
#include "xnmr_ipc.h"      //indirectly dependant on shm_data.h
#include "p_signals.h"
#include "shm_data.h"
#include "buff.h"
#include "xnmr.h"

#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <gtk/gtk.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/*
 *  Global Variables
 */

volatile extern char redraw;
volatile char acq_in_progress;
GtkWidget* start_button;
GtkWidget* start_button_nosave;
GtkWidget* repeat_button;
GtkWidget* repeat_p_button;
GtkWidget* book;
GtkWidget* acq_label;
//GtkWidget* acq_2d_label;
GtkWidget* time_remaining_label;
//GtkWidget* completion_time_label;

int upload_buff = 0;



int  setup_channels(){

  dbuff *buff;

  buff = buffp[current];
  if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(buff->win.but1a))) data_shm->ch1 = 'A';
  if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(buff->win.but1b))) data_shm->ch1 = 'B';
  if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(buff->win.but1c))) data_shm->ch1 = 'C';
  if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(buff->win.but2a))) data_shm->ch2 = 'A';
  if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(buff->win.but2b))) data_shm->ch2 = 'B';
  if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(buff->win.but2c))) data_shm->ch2 = 'C';
  //  printf("setup channels: ch1 %c, ch2: %c\n",data_shm->ch1,data_shm->ch2);
  if (data_shm->ch1 == data_shm->ch2){ 
    popup_msg("Channels 1 & 2 set to same hardware!\nNot proceeding.",TRUE);
    return -1;
  }
  return 0;
}



gint noacq_kill_button_press(GtkWidget *widget, gpointer *data)
{
  popup_msg("Can't kill in noacq mode",TRUE);
  return 0;
}

gint noacq_button_press(GtkWidget *widget, gpointer *data)
{

  // this routine is only connected if we're in noacq mode
  static char norecur = 0;

  if (norecur == 1) {
    norecur = 0;
    return 0;
  }
  popup_msg("Can't Acquire in noacq mode",TRUE);

  norecur = 1;
  gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON( widget ), FALSE );
  return 0;


}


gint kill_button_clicked(GtkWidget *widget, gpointer *data)
{
  int was_in_progress,i,valid,count;
  //  printf("in kill_button_clicked, acq_in_progress=%i\n",acq_in_progress);

  //  printf("in kill clicked, setting not green\nin progress: %i, mode: %i\n",acq_in_progress,data_shm->mode);
  gtk_widget_modify_bg(buffp[upload_buff]->win.ct_box,GTK_STATE_NORMAL,NULL);

  send_sig_acq( ACQ_KILL ); 
  
  if (acq_in_progress == ACQ_STOPPED) return 0;

  /* if acq is listening, sends pulse program a sure death */

  //pop up any buttons which might be down
  was_in_progress = acq_in_progress;
  acq_in_progress = ACQ_STOPPED;

  if (was_in_progress == ACQ_RUNNING) {
    if (data_shm->mode == NORMAL_MODE){
      //      printf("toggling start_button (save)\n");
      gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON( start_button ), FALSE );
    }
    else{
      //      printf("toggling start_button_nosave\n");
      gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON( start_button_nosave ), FALSE );
    }
  }      
  if (was_in_progress == ACQ_REPEATING)
    gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON( repeat_button ), FALSE );
  if (was_in_progress == ACQ_REPEATING_AND_PROCESSING)
    gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON( repeat_p_button ), FALSE );

  // if there were experiments queued, kill them too.
  
  count = 0;
  for (i=0;i<queue.num_queued;i++){
    valid =  gtk_tree_model_get_iter_first(GTK_TREE_MODEL(queue.list),&queue.iter);
    if (valid == 1){
      gtk_list_store_remove(queue.list,&queue.iter);
      count += 1;
    }
    else
      printf("in kill, num_queued is messed up\n");
  }
  queue.num_queued -= count;
  //  printf("killed: %i experiments from the queue\n",count);
  set_queue_label();
  
  queue.num_queued = 0;

  data_shm->mode = NO_MODE;
  return 0;
}

gint start_button_toggled( GtkWidget *widget, gpointer *data )

{

  // in here with data = 0 means acq+save,data = 1 acq nosave
  dbuff* buff;
  GdkColor color;
  
  char s[PATH_LENGTH];
  static char norecur=0;

  //    printf("coming into start_button, norecur is: %i\n",norecur);
  
  
  buff = buffp[ current ];

  if (norecur == 1){
    norecur = 0;
    //    printf("start button: returning on norecur=1\n");
    return 0;
  }

  if (GTK_TOGGLE_BUTTON (widget)->active) {
    /* If control reaches here, the toggle button is down */
    //  printf("widget is active\n");
    if( acq_in_progress == ACQ_STOPPED ) {
      char fileN[ PATH_LENGTH];

      // button coming down and we were stopped.  This is a start.

      if (array_popup_showing == 1){
	popup_msg("Can't start Acquisition with Array window open",TRUE);
	//	printf("trying to start acq with popup showing\n");
	norecur = 1;
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget),FALSE); 
	//	printf("returning on can't start with array open\n");
	return 0;
      }
	
      if (setup_channels() == -1){
	  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget),FALSE); 
	  //	  printf("returning after sending normal start\n");
	  return 0;
      }

      if (widget == start_button ){
	//	printf("setting mode: NORMAL_MODE\n");
	data_shm->mode = NORMAL_MODE;
      }
      else {
	//	printf("setting mode NOSAVE\n");
	data_shm->mode = NORMAL_MODE_NOSAVE;
      }

      acq_process_data = buffp[ current ]->process_data;
      upload_buff = current;
      acq_param_set = current_param_set;

      // should really do this here, I think, and in repeat button, and repeat and process.
      send_paths(); // part of this is repeated below.

      if (data_shm->mode == NORMAL_MODE_NOSAVE) { // means its a start, not start and save
	path_strcpy( s, getenv("HOME"));
	path_strcat( s,"/Xnmr/data/acq_temp");
	path_strcpy( data_shm->save_data_path, s); 
      }
      else  // its a start and save, check to make sure filename is valid
	if (buff->param_set.save_path[strlen(buff->param_set.save_path)-1] == '/'){
	  popup_msg("Invalid file name",TRUE);
	  norecur = 1;
	  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget),FALSE); 
	  //	  printf("returning with invalid file name\n");
	  return 0;
	}

      
      send_acqns();
      send_params();
      data_shm->dwell= buffp[current]->param_set.dwell; // this is what the pulse program will use

      path_strcpy(buffp[current]->path_for_reload,data_shm->save_data_path);
      set_window_title(buffp[current]);

      path_strcpy( fileN, data_shm->save_data_path);


      acq_in_progress = ACQ_RUNNING;
      redraw = 0;

      if (data_shm->mode != NORMAL_MODE_NOSAVE)
	if (check_overwrite(buffp[current],fileN) == FALSE){
	  acq_in_progress = ACQ_STOPPED;
	  norecur = 1;
	  gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON( start_button ), FALSE );
	  return 0;
	}

      //      printf("starting!\n");
      check_buff_size();  
      send_sig_acq( ACQ_START );
      gdk_color_parse("green",&color);
      gtk_widget_modify_bg(buffp[upload_buff]->win.ct_box,GTK_STATE_NORMAL,&color);

      return 0;
        

    }
    // so to be in here at all means the button is coming down, but we're not stopped
    // we just dealt with coming down and ACQ_STOPPED
    else {  
      if (acq_in_progress==ACQ_RUNNING){  // so this is button coming down but already running
	//	printf("coming down but already running\n");
	if ((int) data == 0 && data_shm->mode == NORMAL_MODE) {
	  //	  printf("button coming down but running data == 0\n");
	  return 0;
	}
	if ((int) data == 1 && data_shm->mode == NORMAL_MODE_NOSAVE){
	  //	  printf("button coming down but running data == 1\n");
	  return 0;
	}
	//	printf("button coming down, but wasn't stopped setting norecur, raising button\n");
	norecur=1;
	gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON( widget ), FALSE );
	
      }
         //otherwise, it's an invalid press because another button is down, set the button inactive
      // ie get here if we're repeating
      else{
	//	printf("invalid press because other button is down - we're repeating?\n");
	norecur = 1;
	gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON( widget ), FALSE );	
      }
    }
	
  }
  else {
    //  printf("widget is inactive\n");
    /* If control reaches here, the toggle button is up */
    if( acq_in_progress == ACQ_RUNNING ) {  //a genuine stop
      //      printf("got genuine stop, putting button back down\n");
      norecur = 1;
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget),TRUE); /* and put the
		button back down, it will come back up at the end of this scan */
      send_sig_acq( ACQ_STOP );
    }
      
    else {
      //      printf("setting style nocolor\n");

      gtk_widget_modify_bg(buffp[upload_buff]->win.ct_box,GTK_STATE_NORMAL,NULL);
      data_shm->mode = NO_MODE;
      //      printf("inactive and not running (normal stop?\n");
      /* this is raise of the button was not triggered by the user, 
	 but by a call to gtk_toggle_button_set_active from idle_button_up*/
    } 
  }
  //  printf("got to end of start button\n");
  return 0;
}





gint repeat_button_toggled( GtkWidget *widget, gpointer *data )
{
  GdkColor color;
  static char norecur=0;
  if (norecur == 1){
    norecur = 0;
    return 0;
  }


  if (GTK_TOGGLE_BUTTON (widget)->active) {
    /* If control reaches here, the toggle button is coming down */

    if( acq_in_progress == ACQ_STOPPED ) {
      if (setup_channels() == -1){
	norecur = 1;
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget),FALSE); 
	return 0;
      }
      check_buff_size();
      data_shm->mode = REPEAT_MODE;  
      acq_process_data = buffp[ current ]->process_data;
      //printf( "Sending start repeat signal to ACQ\n" );      
      upload_buff = current;
      acq_param_set = current_param_set;
      // should really do this here, I think, and in repeat button, and repeat and process.
      send_paths();
      send_acqns();
      send_params();
      data_shm->dwell= buffp[current]->param_set.dwell; // this is what the pulse program will use

      //      printf("put %f in shm dwell\n",data_shm->dwell);
      path_strcpy(buffp[current]->path_for_reload,data_shm->save_data_path);
      set_window_title(buffp[current]);

      gdk_color_parse("green",&color);
      gtk_widget_modify_bg(buffp[upload_buff]->win.ct_box,GTK_STATE_NORMAL,&color);

      acq_in_progress = ACQ_REPEATING;
      redraw = 0;
      //      last_draw();
      send_sig_acq( ACQ_START );
    }

    else {     //if this is an invalid press, set the button inactive
      norecur = 1;
      gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON( widget ), FALSE );
    }
	
  }
  else {
      
    //printf( "repeat button lifted\n" );

    /* If control reaches here, the toggle button is up */
    if( acq_in_progress == ACQ_REPEATING ) {  //a genuine stop from the user
      //printf( "Sending stop signal to ACQ\n" );
      // put the button back down though, it will come up on its own at end of scan
      norecur = 1;
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(widget),TRUE);
      send_sig_acq( ACQ_STOP );
    }
      
    else {// it actually comes up here.
      data_shm->mode = NO_MODE;
      gtk_widget_modify_bg(buffp[upload_buff]->win.ct_box,GTK_STATE_NORMAL,NULL);

    }
  }
  return TRUE;
  
}

gint repeat_p_button_toggled( GtkWidget *widget, gpointer *data )
{

  GdkColor color;
  static char norecur = 0;
  if (norecur == 1){
    norecur = 0;
    return 0;
  }

  if (GTK_TOGGLE_BUTTON (widget)->active) {
    /* If control reaches here, the toggle button is down */

    if( acq_in_progress == ACQ_STOPPED ) {
      //printf( "Sending start repeat and process signal to ACQ\n" );
      if (setup_channels() == -1) {
	norecur = 1;
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget),FALSE); 
	return 0;
      }
      check_buff_size();
      data_shm->mode = REPEAT_MODE;        
      acq_process_data = buffp[ current ]->process_data;
      upload_buff = current;
      acq_param_set = current_param_set;
      // should really do this here, I think, and in repeat button, and repeat and process.
      send_paths();
      send_acqns();
      send_params();
      data_shm->dwell= buffp[current]->param_set.dwell; // this is what the pulse program will use

      //      printf("put %f in shm dwell\n",data_shm->dwell);
      gdk_color_parse("green",&color);
      gtk_widget_modify_bg(buffp[upload_buff]->win.ct_box,GTK_STATE_NORMAL,&color);

      acq_in_progress = ACQ_REPEATING_AND_PROCESSING;
      redraw = 0;

      //      last_draw();
      send_sig_acq( ACQ_START );
    }

    else {     //if this is an invalid press, set the button inactive
      norecur = 1;
      gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON( widget ), FALSE );
      
    }
  }

  else {
      
    /* If control reaches here, the toggle button is up */
    if( acq_in_progress == ACQ_REPEATING_AND_PROCESSING ) {  //a genuine stop
      //printf( "Sending stop signal to ACQ\n" );
      norecur = 1;
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(widget),TRUE);
      send_sig_acq( ACQ_STOP );
    }
      
    else { // button legitimately coming up.
      data_shm->mode = NO_MODE;
      gtk_widget_modify_bg(buffp[upload_buff]->win.ct_box,GTK_STATE_NORMAL,NULL);
    }
  }
  return 0;

}



GtkWidget* create_panels()

{
  GtkWidget *pantable, *page, *label, *button;

  acq_in_progress = ACQ_STOPPED;
 
  //printf( "creating control panel\n" );

  pantable = gtk_table_new(9,3,FALSE);
  book = gtk_notebook_new();

  gtk_notebook_set_tab_pos(GTK_NOTEBOOK(book),GTK_POS_BOTTOM);
  gtk_table_attach_defaults(GTK_TABLE(pantable),book,0,2,0,9);
  gtk_widget_show(book);

  label=gtk_label_new( "Process" );
  gtk_widget_show(label);

  page = create_process_frame();

  gtk_notebook_append_page(GTK_NOTEBOOK(book),page,label);

  label=gtk_label_new("Process 2d");
  gtk_widget_show(label);
  page = create_process_frame_2d();
  gtk_notebook_append_page(GTK_NOTEBOOK(book),page,label);

  label = gtk_label_new( "Parameters" );
  gtk_widget_show(label);
  
  page = create_parameter_frame();

  gtk_notebook_prepend_page(GTK_NOTEBOOK(book),page,label);


  gtk_notebook_set_current_page(GTK_NOTEBOOK(book),0);
  gtk_widget_show(pantable);

  start_button = gtk_toggle_button_new_with_label( "Acquire and Save" );

  if(no_acq == FALSE){
  
    if( data_shm->mode == NORMAL_MODE && data_shm->acq_sig_ui_meaning != ACQ_DONE) {
      gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON( start_button ), TRUE );
      acq_in_progress = ACQ_RUNNING;
    }
    g_signal_connect(G_OBJECT(start_button),"toggled",G_CALLBACK(start_button_toggled), NULL);
  }
  else
    g_signal_connect (G_OBJECT(start_button),"toggled",G_CALLBACK(noacq_button_press),NULL);
  gtk_table_attach_defaults(GTK_TABLE(pantable),start_button,2,3,0,1);
  gtk_widget_show(start_button);



  start_button_nosave = gtk_toggle_button_new_with_label( "Acquire" );

  if(no_acq == FALSE){
    if( data_shm->mode == NORMAL_MODE_NOSAVE && data_shm->acq_sig_ui_meaning != ACQ_DONE) {
      gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON( start_button_nosave ), TRUE );
      acq_in_progress = ACQ_RUNNING;
    }
    g_signal_connect(G_OBJECT(start_button_nosave),"toggled",G_CALLBACK(start_button_toggled),NULL);
  }
  else
    g_signal_connect (G_OBJECT(start_button_nosave),"toggled",G_CALLBACK(noacq_button_press),NULL);
  gtk_table_attach_defaults(GTK_TABLE(pantable),start_button_nosave,2,3,1,2);
  gtk_widget_show(start_button_nosave);
  



  repeat_button = gtk_toggle_button_new_with_label( "Repeat" );
  
  if(no_acq ==FALSE){
  
    if( data_shm->mode == REPEAT_MODE ) {
      gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON( repeat_button ), TRUE );
      acq_in_progress = ACQ_REPEATING;
    }

    g_signal_connect(G_OBJECT(repeat_button),"toggled",G_CALLBACK(repeat_button_toggled), NULL);
  }
  else
    g_signal_connect (G_OBJECT(repeat_button),"toggled",G_CALLBACK(noacq_button_press),NULL);
  gtk_table_attach_defaults(GTK_TABLE(pantable),repeat_button,2,3,2,3);
  gtk_widget_show(repeat_button);

  button = gtk_button_new_with_label( "Process" );
  g_signal_connect(G_OBJECT(button),"clicked",G_CALLBACK(process_data), NULL);
  gtk_table_attach_defaults(GTK_TABLE(pantable),button,2,3,3,4);
  gtk_widget_show(button);

  repeat_p_button = gtk_toggle_button_new_with_label( "Repeat and Process" );
  if(no_acq ==FALSE){
    g_signal_connect(G_OBJECT(repeat_p_button),"toggled",G_CALLBACK(repeat_p_button_toggled), NULL);
  }
  else
    g_signal_connect (G_OBJECT(repeat_p_button),"toggled",G_CALLBACK(noacq_button_press),NULL);
  gtk_table_attach_defaults(GTK_TABLE(pantable),repeat_p_button,2,3,4,5);
  gtk_widget_show(repeat_p_button);

  button = gtk_button_new_with_label("Reload");
  g_signal_connect( G_OBJECT( button ), "clicked", G_CALLBACK( reload ), NULL );
  gtk_table_attach_defaults(GTK_TABLE(pantable),button,2,3,5,6);
  gtk_widget_show(button);



  button = gtk_button_new_with_label("Kill");
  if( no_acq == FALSE ){
  g_signal_connect( G_OBJECT( button ), "clicked", G_CALLBACK( kill_button_clicked ), NULL );
  }
  else
    g_signal_connect(G_OBJECT(button),"clicked", G_CALLBACK(noacq_kill_button_press), NULL);
  gtk_table_attach_defaults(GTK_TABLE(pantable),button,2,3,6,7);
  gtk_widget_show(button);

  if ( no_acq == FALSE )
    acq_label = gtk_label_new( "Acq Stopped\n " );
  else
    acq_label = gtk_label_new( "NoAcq mode" );
  gtk_table_attach_defaults( GTK_TABLE(pantable),acq_label,2,3,7,8 );
  gtk_widget_show( acq_label );

  /*
  acq_2d_label = gtk_label_new( "" );
  gtk_table_attach_defaults( GTK_TABLE(pantable),acq_2d_label,2,3,8,9 );
  gtk_widget_show( acq_2d_label );
  */

  time_remaining_label = gtk_label_new( " \n \n " );
  gtk_table_attach_defaults( GTK_TABLE(pantable),time_remaining_label,2,3,8,9 );
  gtk_widget_show( time_remaining_label );

  /*  completion_time_label = gtk_label_new( "completion:\n" );
  gtk_table_attach_defaults( GTK_TABLE(pantable),completion_time_label,2,3,10,11);
  gtk_widget_show(completion_time_label);
  */

  return pantable;
  
}





void check_buff_size(){
  /* two places this number can get reset from which are awkward:
     here, and in upload routine */

  if (buffp[current]->acq_npts > MAX_DATA_POINTS)
    buffp[current]->acq_npts=MAX_DATA_POINTS;
  buff_resize( buffp[ current ], buffp[current]->acq_npts, 1 );


  data_shm->npts=buffp[current]->acq_npts;

  /* also set the new size in the box
     don't need to make sure that current set 
     is acq set because it must be */
  update_npts(buffp[current]->param_set.npts);
}
