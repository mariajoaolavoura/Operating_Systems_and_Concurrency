#include <stdlib.h>
#include "./include/thread.h"
#include "dbc.h"
#include "global.h"
#include "utils.h"
#include "box.h"
#include "timer.h"
#include "logger.h"
#include "barber-shop.h"
#include "barber.h"
#include "communication-line.h"

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
static void process_haircut_request(Barber* barber, int shaveReq);
static void pick_haircut_tools(Barber* barber);
static void return_haircut_tools(Barber* barber);
static void process_shave_request(Barber* barber);
static void pick_shave_tools(Barber* barber);
static void return_shave_tools(Barber* barber);
static void process_wash_hair_request(Barber* barber);

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
	mutex_lock(&barber->shop->loggerMutex);
	send_log(barber->logId, to_string_barber(barber));
	mutex_unlock(&barber->shop->loggerMutex);
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
	/**
	 * 1: sit in a random empty seat in barber bench (always available)
	 ** TODO:
	 **/
	require (barber != NULL, "barber argument required");

	mutex_lock(&barber->shop->barberBenchMutex);
	require (num_seats_available_barber_bench(barber_bench(barber->shop)) > 0, "seat not available in barber shop");
	require (!seated_in_barber_bench(barber_bench(barber->shop), barber->id), "barber already seated in barber shop");

	int seatPos = random_sit_in_barber_bench(barber_bench(barber->shop), barber->id);
	mutex_unlock(&barber->shop->barberBenchMutex);
	barber->benchPosition = seatPos;

	//printf("--------------------------------------------BARBER LIFE - SIT IN BARBER BENCH\n");
	log_barber(barber);
}

static void wait_for_client(Barber* barber)
{
	/**
	 * 1: set the barber state to WAITING_CLIENTS
	 * 2: get next client from client benches (if empty, wait) (also, it may be required to check for simulation termination)
	 * 3: receive and greet client (receive its requested services, and give back the barber's id)
	 ** TODO:
	 **/

	require (barber != NULL, "barber argument required");

	barber->state = WAITING_CLIENTS;
	// TODO: problema do fecho da loja
	RQItem requests = next_client_in_benches(&(barber->shop->clientBenches));

	mutex_lock(&barber->shop->shopFloorMutex);
	if (!barber->shop->opened){
		mutex_unlock(&barber->shop->shopFloorMutex);
		log_barber(barber);
		return;
	}
	mutex_unlock(&barber->shop->shopFloorMutex);

	barber->reqToDo = requests.request;
	barber->clientID = requests.clientID;

	receive_and_greet_client(barber->shop, barber->id, barber->clientID);

	log_barber(barber);
}

static int work_available(Barber* barber)
{
	/**
	 * TODO:
	 * 1: find a safe way to solve the problem of barber termination
	 **/

	require (barber != NULL, "barber argument required");

	mutex_lock(&barber->shop->shopFloorMutex);
	if (!(barber->shop->opened)){

		rise_from_barber_bench(barber);
		barber->benchPosition = -1;
		mutex_unlock(&barber->shop->shopFloorMutex);
		//printf("!!!!!!!!!!!!!!!!!!!! barber %d, no work available, suicides\n", barber->id);
		return 0;
	}
	mutex_unlock(&barber->shop->shopFloorMutex);

	if (barber->clientID > 0){
		return 1;
	}

	return 0;
}

static void rise_from_barber_bench(Barber* barber)
{
	/**
	 * 1: rise from the seat of barber bench
	 ** TODO:
	 **/

	require (barber != NULL, "barber argument required");

	mutex_lock(&barber->shop->barberBenchMutex);
	require (seated_in_barber_bench(barber_bench(barber->shop), barber->id), "barber not seated in barber shop");
	
	rise_barber_bench(&(barber->shop->barberBench), barber->benchPosition);
	mutex_unlock(&barber->shop->barberBenchMutex);
	barber->benchPosition = -1;
	//printf("--------------------------------------------BARBER LIFE - RISE FROM BARBER BENCH\n");
	log_barber(barber);
}


static void process_resquests_from_client(Barber* barber)
{
	/**
	 * Process one client request at a time, until all requests are fulfilled.
	 * For each request:
	 * 1: select the request to process (any order is acceptable)
	 * 2: reserve the chair/basin for the service (setting the barber's state accordingly)
	 *    2.1: set the client state to a proper value
	 *    2.2: reserve a random empty chair/basin
	 *    2.2: inform client on the service to be performed
	 * 3: depending on the service, grab the necessary tools from the pot (if any)
	 * 4: process the service (see [incomplete] process_haircut_request as an example)
	 *  4.1: return the used tools to the pot (if any)
	 *
	 * At the end the client must leave the barber shop
	 **TODO:
	 **/

	require (barber != NULL, "barber argument required");

	//1: select the request to process (any order is acceptable)
	Service service;
	if ((barber->reqToDo & HAIRCUT_REQ) || (barber->reqToDo & SHAVE_REQ)) {

		//reserve a random empty chair
		barber->state = WAITING_BARBER_SEAT;
		int chairPos = reserve_empty_barber_chair(barber->shop, barber->id);
		barber->chairPosition = chairPos;

		if (barber->reqToDo & HAIRCUT_REQ){

			set_barber_chair_service(&service, barber->id, barber->clientID, chairPos, HAIRCUT_REQ);
			inform_client_on_service(barber->shop, service);

			process_haircut_request(barber, barber->reqToDo & SHAVE_REQ);
		}

		if (barber->reqToDo & SHAVE_REQ){

			set_barber_chair_service(&service, barber->id, barber->clientID, chairPos, SHAVE_REQ);
			inform_client_on_service(barber->shop, service);

			process_shave_request(barber);
		}
	}

	if (barber->reqToDo & WASH_HAIR_REQ) {

		barber->state = WAITING_WASHBASIN;

		//TODO: QUAL BLOQUEAR? SEMAFORO COMO?
		int basinPos = reserve_empty_washbasin(barber->shop, barber->id);
		barber->basinPosition = basinPos;

		set_washbasin_service(&service, barber->id, barber->clientID, basinPos);
		inform_client_on_service(barber->shop, service);

		process_wash_hair_request(barber);
	}

	barber->state = NONE;
	//printf("--------------------------------------------BARBER LIFE - PROCESS REQUESTS FROM CLIENT\n");
	log_barber(barber);
}


static void release_client(Barber* barber)
{
	/**
	 * 1: notify client the all the services are done
	 ** TODO:
	 **/

	require (barber != NULL, "barber argument required");

	client_done(barber->shop, barber->clientID); // is a communicationLine action (messagesMutex inside)

	mutex_lock(&barber->shop->shopFloorMutex);
	while (is_client_inside(barber->shop, barber->clientID)){
		cond_wait(&barber->shop->clientLeft, &barber->shop->shopFloorMutex);
	}
	mutex_unlock(&barber->shop->shopFloorMutex);
	barber->clientID = -1;
	//printf("--------------------------------------------BARBER LIFE - RELEASE CLIENT\n");
	log_barber(barber);
}


static void done(Barber* barber)
{
	/**
	 * 1: set the barber state to DONE
	 ** TODO:
	 **/
	require (barber != NULL, "barber argument required");

	barber->state = DONE;
	//printf("--------------------------------------------BARBER LIFE - EXITING - DONE\n");
	log_barber(barber);
}


static void process_haircut_request(Barber* barber, int shaveReq)
{
	/**
	 * ([incomplete] example code for task completion algorithm)
	 ** TODO:
	 **/
	require (barber != NULL, "barber argument required");
	//   require (barber->tools & SCISSOR_TOOL, "barber not holding a scissor");
	//   require (barber->tools & COMB_TOOL, "barber not holding a comb");

	//grab the necessary tools from the pot
	pick_haircut_tools(barber);

	//wait for client to sit
	BarberChair* chair = barber->shop->barberChair+barber->chairPosition;
	set_tools_barber_chair(chair, barber->tools);

	//process the service
	barber->state = CUTTING;

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

	if(!shaveReq){
		release_barber_barberchair(barber->shop, barber->id, barber->chairPosition);
		barber->chairPosition = -1;
	}

	return_haircut_tools(barber);

	log_barber(barber);  // (if necessary) more than one in proper places!!!
}


static void pick_haircut_tools(Barber* barber)
{
	require (barber != NULL, "barber argument required");

	barber->state = REQ_SCISSOR;
	pick_scissor(&barber->shop->toolsPot);
	barber->tools += SCISSOR_TOOL;

	barber->state = REQ_COMB;
	pick_comb(&barber->shop->toolsPot);
	barber->tools += COMB_TOOL;
}


static void return_haircut_tools(Barber* barber)
{
	require (barber != NULL, "barber argument required");

	return_scissor(&barber->shop->toolsPot);
	return_comb(&barber->shop->toolsPot);
	barber->tools = NO_TOOLS;
}


static void process_shave_request(Barber* barber)
{
	/**
	 * ([incomplete] example code for task completion algorithm)
	 **TODO:
	 **/
	require (barber != NULL, "barber argument required");

	//grab the necessary tools from the pot
	pick_shave_tools(barber);

	//wait for client to sit, or to signal ready
	BarberChair* chair = &barber->shop->barberChair[barber->chairPosition];
	set_tools_barber_chair(chair, barber->tools);

	//process the service
	barber->state = SHAVING;

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

	release_barber_barberchair(barber->shop, barber->id, barber->chairPosition);
	barber->chairPosition = -1;

	return_shave_tools(barber);

	log_barber(barber);
}


static void pick_shave_tools(Barber* barber)
{
	require (barber != NULL, "barber argument required");

	barber->state = REQ_RAZOR;
	pick_razor(&barber->shop->toolsPot);
	barber->tools = RAZOR_TOOL;
}


static void return_shave_tools(Barber* barber)
{
	require (barber != NULL, "barber argument required");

	return_razor(&barber->shop->toolsPot);
	barber->tools = NO_TOOLS;
}


static void process_wash_hair_request(Barber* barber)
{
	/**
	 * ([incomplete] example code for task completion algorithm)
	 **TODO:
	 **/
	require (barber != NULL, "barber argument required");

	//wait for client to sit
	wait_for_client_to_sit_in_washbasin(barber->shop, barber->basinPosition);

	//process the service
	barber->state = WASHING;

	int steps = random_int(5,20);
	int slice = (global->MAX_WORK_TIME_UNITS-global->MIN_WORK_TIME_UNITS+steps)/steps;
	int complete = 0;
	while(complete < 100)
	{
		spend(slice);
		complete += 100/steps;
		if (complete > 100)
			complete = 100;
		set_completion_washbasin(washbasin(barber->shop, barber->basinPosition), complete);
	}

	//release washbasin
	release_barber_washbasin(barber->shop, barber->id, barber->basinPosition);
	barber->basinPosition = -1;

	log_barber(barber);  // (if necessary) more than one in proper places!!!
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

