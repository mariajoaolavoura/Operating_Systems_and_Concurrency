#include <stdlib.h>
#include "dbc.h"
#include "global.h"
#include "utils.h"
#include "box.h"
#include "timer.h"
#include "logger.h"
#include "barber-shop.h"
#include "barber.h"

enum State
{
   NONE = 0,
   CUTTING,
   SHAVING,
   WASHING,
   WAITING_CLIENTS,
   WAITING_BARBER_SEAT,
   WAITING_WASHBASIN,
   REQ_SCISSOR,
   REQ_COMB,
   REQ_RAZOR,
   DONE
};

#define State_SIZE (DONE - NONE + 1)

static const char* stateText[State_SIZE] =
{
   "---------",
   "CUTTING  ",
   "SHAVING  ",
   "WASHING  ",
   "W CLIENT ", // Waiting for client
   "W SEAT   ", // Waiting for barber seat
   "W BASIN  ", // Waiting for washbasin
   "R SCISSOR", // Request a scissor
   "R COMB   ", // Request a comb
   "R RAZOR  ", // Request a razor
   "DONE     ",
};

static const char* skel = 
   "@---+---+---@\n"
   "|B##|C##|###|\n"
   "+---+---+-+-+\n"
   "|#########|#|\n"
   "@---------+-@";
static int skel_length = num_lines_barber()*(num_columns_barber()+1)*4; // extra space for (pessimistic) utf8 encoding!

static void life(Barber* barber);

static void sit_in_barber_bench(Barber* barber);
static void wait_for_client(Barber* barber);
static int work_available(Barber* barber);
static void rise_from_barber_bench(Barber* barber);
static void process_resquests_from_client(Barber* barber);
static void release_client(Barber* barber);
static void done(Barber* barber);
static void process_haircut_request(Barber* barber);
static void process_hairwash_request(Barber* barber);
static void process_shave_request(Barber* barber);

static char* to_string_barber(Barber* barber);

size_t sizeof_barber()
{
   return sizeof(Barber);
}

int num_lines_barber()
{
   return string_num_lines((char*)skel);
}

int num_columns_barber()
{
   return string_num_columns((char*)skel);
}

void init_barber(Barber* barber, int id, BarberShop* shop, int line, int column)
{
   require (barber != NULL, "barber argument required");
   require (id > 0, concat_3str("invalid id (", int2str(id), ")"));
   require (shop != NULL, "barber shop argument required");
   require (line >= 0, concat_3str("Invalid line (", int2str(line), ")"));
   require (column >= 0, concat_3str("Invalid column (", int2str(column), ")"));

   barber->id = id;
   barber->state = NONE;
   barber->shop = shop;
   barber->clientID = 0;
   barber->reqToDo = 0;
   barber->benchPosition = -1;
   barber->chairPosition = -1;
   barber->basinPosition = -1;
   barber->tools = 0;
   barber->internal = NULL;
   barber->logId = register_logger((char*)("Barber:"), line ,column,
                                   num_lines_barber(), num_columns_barber(), NULL);
}

void term_barber(Barber* barber)
{
   require (barber != NULL, "barber argument required");

   if (barber->internal != NULL)
   {
      mem_free(barber->internal);
      barber->internal = NULL;
   }
}

void log_barber(Barber* barber)
{
   require (barber != NULL, "barber argument required");

   spend(random_int(global->MIN_VITALITY_TIME_UNITS, global->MAX_VITALITY_TIME_UNITS));
   send_log(barber->logId, to_string_barber(barber));
}

void* main_barber(void* args)
{
   Barber* barber = (Barber*)args;
   require (barber != NULL, "barber argument required");
   life(barber);
   return NULL;
}

static void life(Barber* barber)
{
   require (barber != NULL, "barber argument required");

   sit_in_barber_bench(barber);
   wait_for_client(barber);
   while(work_available(barber)) // no more possible clients and closes barbershop
   {
      rise_from_barber_bench(barber);
      process_resquests_from_client(barber);
      release_client(barber);
      sit_in_barber_bench(barber);
      wait_for_client(barber);
   }
   done(barber);
}

static void sit_in_barber_bench(Barber* barber)
{
   /** TODO:
    * 1: sit in a random empty seat in barber bench (always available)
    **/

   require (barber != NULL, "barber argument required");
   require (num_seats_available_barber_bench(barber_bench(barber->shop)) > 0, "seat not available in barber shop");
   require (!seated_in_barber_bench(barber_bench(barber->shop), barber->id), "barber already seated in barber shop");

	 barber->benchPosition = random_sit_in_barber_bench(barber_bench(barber->shop),barber->id);

   log_barber(barber);
}

static void wait_for_client(Barber* barber)
{
   /** TODO:
    * 1: set the barber state to WAITING_CLIENTS
    * 2: get next client from client benches (if empty, wait) (also, it may be required to check for simulation termination)
    * 3: receive and greet client (receive its requested services, and give back the barber's id)
    **/

   require (barber != NULL, "barber argument required");

	 barber->state = WAITING_CLIENTS;
	 while(client_benches(barber->shop)->numSeats == num_available_benches_seats(client_benches(barber->shop)));
	 // { if(no more clients need to enter) -> close shop }
	 RQItem queue_item = next_client_in_benches(client_benches(barber->shop));
	 RQItem* tmp_qitem = &(queue_item);
	 receive_and_greet_client(barber->shop,barber->id,tmp_qitem->clientID);

   log_barber(barber);  // (if necessary) more than one in proper places!!!
}

static int work_available(Barber* barber)
{
   /** TODO:
    * 1: find a safe way to solve the problem of barber termination
    **/

   require (barber != NULL, "barber argument required");

	 if(shop_opened(barber->shop))
	 	 return 1;

   return 0;
}

static void rise_from_barber_bench(Barber* barber)
{
   /** TODO:
    * 1: rise from the seat of barber bench
    **/

   require (barber != NULL, "barber argument required");
   require (seated_in_barber_bench(barber_bench(barber->shop), barber->id), "barber not seated in barber shop");

	 rise_barber_bench(barber_bench(barber->shop),barber->benchPosition);
	 barber->benchPosition = -1;

   log_barber(barber);
}

static void process_resquests_from_client(Barber* barber)
{
   /** TODO:
    * Process one client request at a time, until all requests are fulfilled.
    * For each request:
    * 1: select the request to process (any order is acceptable)
    * 2: reserve the chair/basin for the service (setting the barber's state accordingly) 
    *    2.1: set the client state to a proper value
    *    2.2: reserve a random empty chair/basin 
    *    2.3: inform client on the service to be performed
    * 3: depending on the service, grab the necessary tools from the pot (if any)
    * 4: process the service (see [incomplete] process_haircut_request as an example)
    * 5: return the used tools to the pot (if any)
    *
    *
    * At the end the client must leave the barber shop
    **/

   require (barber != NULL, "barber argument required");

	 int current_request = 0;
	 while(barber->reqToDo > 0) {
	 	 if(barber->reqToDo >= 4)
	 	 	 current_request = 4;
	 	 else if(barber->reqToDo >= 2)
	 	 	 current_request = 2;
	 	 else
	 	 	 current_request = 1;
	 	 
	 	 barber->reqToDo -= current_request;
     
	 	 Service service_to_send;

	 	 if(current_request == 1) {
	 	 	 ensure(barber->reqToDo == 0,"Post-condition not met: reqToDo must be 0!");
	 	 	 while(num_available_barber_chairs(barber->shop) == 0);
	 	 	 barber->chairPosition = reserve_random_empty_barber_chair(barber->shop,barber->id);
       barber->state = CUTTING;
	 	 }
	 	 else if(current_request == 2) {
	 	 	 while(num_available_washbasin(barber->shop) == 0);
	 	 	 barber->basinPosition = reserve_random_empty_washbasin(barber->shop,barber->id);
	 	 	 barber->state = WASHING;
	 	 }
	 	 else {
	 	 	 while(num_available_barber_chairs(barber->shop) == 0);
	 	 	 barber->chairPosition = reserve_random_empty_barber_chair(barber->shop,barber->id);
	 	 	 barber->state = SHAVING;
	 	 }

     log_barber(barber);

	 	 if(current_request == 1 || current_request == 4)
	 	 	 set_barber_chair_service(&service_to_send,barber->id,barber->clientID,barber->chairPosition,current_request);
	 	 else
	 	 	 set_washbasin_service(&service_to_send,barber->id,barber->clientID,barber->basinPosition);

	 	 inform_client_on_service(barber->shop,service_to_send);

     BarberChair* bbchair = barber_chair(barber->shop,barber->chairPosition);

	 	 if(current_request == 1) {
       barber->state = REQ_SCISSOR;
       log_barber(barber);
       while((tools_pot(barber->shop))->availScissors == 0);
	 	 	 pick_scissor(tools_pot(barber->shop));
	 	 	 barber->tools += 1;
       bbchair->toolsHolded += 1;

       barber->state = REQ_COMB;
       log_barber(barber);
	 	 	 while((tools_pot(barber->shop))->availCombs == 0);
	 	 	 pick_comb(tools_pot(barber->shop));
	 	 	 barber->tools += 2;
       bbchair->toolsHolded += 2;
	 	 	 ensure(barber->tools == 3,"Post-condition not met: barber->tools must be 3!");
       ensure(bbchair->toolsHolded == 3,"Post-condition not met: bbchair->toolsHolded must be 3!");
	 	 }
	 	 else if(current_request == 4) {
       barber->state = REQ_RAZOR;
       log_barber(barber);
       while((tools_pot(barber->shop))->availRazors == 0);
	 	 	 pick_razor(tools_pot(barber->shop));
	 	 	 barber->tools += 4;
       bbchair->toolsHolded += 4;
	 	 	 ensure(barber->tools == 4,"Post-condition not met: barber->tools must be 4!");
       ensure(bbchair->toolsHolded == 4,"Post-condition not met: bbchair->toolsHolded must be 4!");
	 	 }

     if(current_request == 1) {
       barber->state = CUTTING;
       log_barber(barber);
       process_haircut_request(barber);
     }
     else if(current_request == 2) {
       barber->state = WASHING;
       log_barber(barber);
       process_hairwash_request(barber);
     }
     else {
       barber->state = SHAVING;
       log_barber(barber);
       process_shave_request(barber);
     }

     if(current_request == 1) {
       return_scissor(tools_pot(barber->shop));
       barber->tools -= 1;
       bbchair->toolsHolded -= 1;

       return_comb(tools_pot(barber->shop));
       barber->tools -= 2;
       bbchair->toolsHolded -= 2;
     }
     else if(current_request == 4) {
       return_razor(tools_pot(barber->shop));
       barber->tools -= 4;
       bbchair->toolsHolded -= 4;
     }

     ensure(barber->tools == 0,"Post-condition not met: barber->tools must be 0!");
     ensure(bbchair->toolsHolded == 0,"Post-condition not met: bbchair->toolsHolded must be 0!");
	 }

   release_client(barber);
   log_barber(barber);  // (if necessary) more than one in proper places!!!
}

static void release_client(Barber* barber)
{
   /** TODO:
    * 1: notify client that all the services are done
    **/

   require (barber != NULL, "barber argument required");

   done(barber);
   barber->clientID = 0;

   log_barber(barber);
}

static void done(Barber* barber)
{
   /** TODO:
    * 1: set the client state to DONE
    **/
   require (barber != NULL, "barber argument required");

   client_done(barber->shop,barber->clientID);

   log_barber(barber);
}

static void process_haircut_request(Barber* barber)
{
   /** TODO:
    * ([incomplete] example code for task completion algorithm)
    **/
   require (barber != NULL, "barber argument required");
   require (barber->tools & SCISSOR_TOOL, "barber not holding a scissor");
   require (barber->tools & COMB_TOOL, "barber not holding a comb");

   int steps = random_int(5,20);
   int slice = (global->MAX_WORK_TIME_UNITS-global->MIN_WORK_TIME_UNITS+steps)/steps;
   int complete = 0;
   while(complete < 100)
   {
      spend(slice);
      complete += 100/steps;
      if (complete > 100)
         complete = 100;
      set_completion_barber_chair(barber_chair(barber->shop, barber->chairPosition), complete);
   }

   log_barber(barber);  // (if necessary) more than one in proper places!!!
}

static void process_hairwash_request(Barber* barber)
{

}

static void process_shave_request(Barber* barber)
{

}

static char* to_string_barber(Barber* barber)
{
   require (barber != NULL, "barber argument required");

   if (barber->internal == NULL)
      barber->internal = (char*)mem_alloc(skel_length + 1);

   char tools[4];
   tools[0] = (barber->tools & SCISSOR_TOOL) ? 'S' : '-',
      tools[1] = (barber->tools & COMB_TOOL) ?    'C' : '-',
      tools[2] = (barber->tools & RAZOR_TOOL) ?   'R' : '-',
      tools[3] = '\0';

   char* pos = (char*)"-";
   if (barber->chairPosition >= 0)
      pos = int2nstr(barber->chairPosition+1, 1);
   else if (barber->basinPosition >= 0)
      pos = int2nstr(barber->basinPosition+1, 1);

   return gen_boxes(barber->internal, skel_length, skel,
         int2nstr(barber->id, 2),
         barber->clientID > 0 ? int2nstr(barber->clientID, 2) : "--",
         tools, stateText[barber->state], pos);
}

