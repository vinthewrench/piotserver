//
//  macos_gpiod.c
//  coopserver
//
//  Created by Vincent Moscaritolo on 12/24/21.
//

#include <stdio.h>
#include <errno.h>
#include <time.h>

#if defined(__APPLE__)
// used for cross compile on osx
#include <sstream>
#include "LogMgr.hpp"

#include "macos_gpiod.h"

struct gpiod_chip {
	struct gpiod_line **lines;
	unsigned int num_lines;

	int fd;

	char name[32];
	char label[32];
};

struct gpiod_line {
	unsigned int offset;

	/* The direction of the GPIO line. */
	int direction;

	/* The active-state configuration. */
	int active_state;

	/* The logical value last written to the line. */
	int output_value;

	/* The GPIOLINE_FLAGs returned by GPIO_GET_LINEINFO_IOCTL. */
	uint32_t info_flags;

	/* The GPIOD_LINE_REQUEST_FLAGs provided to request the line. */
	uint32_t req_flags;

	/*
	 * Indicator of LINE_FREE, LINE_REQUESTED_VALUES or
	 * LINE_REQUESTED_EVENTS.
	 */
	int state;

	struct gpiod_chip *chip;
	struct line_fd_handle *fd_handle;

	char name[32];
	char consumer[32];
};


struct gpiod_chip *gpiod_chip_open(const char *path) {
	
 //   LOGT_DEBUG("gpiod_chip_open(%s)",path );
	 
	struct gpiod_chip * chip = (gpiod_chip*) malloc(sizeof (struct gpiod_chip));
  
	return  chip;
};

struct gpiod_chip *gpiod_chip_open_by_name(const char *name){
    
//    LOGT_DEBUG("gpiod_chip_open_by_name(%s)",name );
     
    struct gpiod_chip * chip =  (gpiod_chip*) malloc(sizeof (struct gpiod_chip));
  
    return  chip;
};

void gpiod_chip_close(struct gpiod_chip *chip){
	if(chip){
//        LOGT_DEBUG("gpiod_chip_close" );
		free(chip);
	}
}

int gpiod_chip_get_lines(struct gpiod_chip *chip,
			 unsigned int *offsets, unsigned int num_offsets,
			 struct gpiod_line_bulk *bulk){
	
	bulk->num_lines = num_offsets;
	
//    LOGT_DEBUG("gpiod_chip_get_lines" );
	return 0;
}

void gpiod_line_release_bulk(struct gpiod_line_bulk *bulk) {
	 
 //   LOGT_DEBUG("gpiod_line_release_bulk" );
}
 
int gpiod_line_request_bulk(struct gpiod_line_bulk *bulk,
				 const struct gpiod_line_request_config *config,
				 const int *default_vals){
	
 //   LOGT_DEBUG("gpiod_line_request_bulk" );
	return 0;
}
 

int gpiod_line_get_value_bulk(struct gpiod_line_bulk *bulk, int *values){
	
//	LOGT_DEBUG("gpiod_line_get_value_bulk\n" );

	for(int i = 0; i < bulk->num_lines; i++){
		values[i] = 0;
	}
	return 0;
}


int gpiod_line_set_value_bulk(struct gpiod_line_bulk *bulk, const int *values) {
    std::ostringstream oss;

    oss << ("gpiod_line_set_value_bulk( " );

 	for(int i = 0; i < bulk->num_lines; i++){
       oss << ( values[i] == 0?"OFF ":"ON ");
 	}
    oss << (")");
//    LOGT_DEBUG("%s", oss.str().c_str());
    
	return 0;
}


//struct gpiod_line *
//gpiod_chip_get_line(struct gpiod_chip *chip, unsigned int offset) GPIOD_API;

struct gpiod_line *gpiod_chip_get_line(struct gpiod_chip *chip, unsigned int offset) {
	
//    LOGT_DEBUG("gpiod_chip_get_line(%d)",offset);
	 
	struct gpiod_line * line = (gpiod_line*) malloc(sizeof (struct gpiod_line));
  
	return  line;
};


void gpiod_line_release(struct gpiod_line *line){
	if(line){
//        LOGT_DEBUG("gpiod_line_release" );
		free(line);
	}
}

int gpiod_line_request_input_flags(struct gpiod_line *line,
                    const char *consumer, int flags)
{
    
//    LOGT_DEBUG("gpiod_line_request_input_flags()");
     return 0;
}


int gpiod_line_request_falling_edge_events_flags(struct gpiod_line *line,
						 const char *consumer,
						 int flags)
{
	
//    LOGT_DEBUG("gpiod_line_request_falling_edge_events_flags()");
 	return 0;
}


int gpiod_line_event_read(struct gpiod_line *line,
								  struct gpiod_line_event *event)  {
//    LOGT_DEBUG("gpiod_line_event_read()");
	return 0;
}

int gpiod_line_event_wait(struct gpiod_line *line,
								  const struct timespec *timeout) {
	
	nanosleep(timeout, NULL);
	
	//	LOGT_DEBUG("gpiod_line_event_wait()\n");
	
	return 0;
}

 
int gpiod_line_request_output_flags(struct gpiod_line *line,
                                        const char *consumer, int flags,
                                        int default_val) {
    
//    LOGT_DEBUG("gpiod_line_request_output_flags()");
    return 0;
}

int gpiod_line_request_output(struct gpiod_line *line,
					const char *consumer, int default_val) {
	
//    LOGT_DEBUG("gpiod_line_request_output()");
	return 0;
}

int gpiod_line_set_value(struct gpiod_line *line, int value){
 
//    LOGT_DEBUG("gpiod_line_set_value()");
	return 0;

}
int gpiod_line_get_value(struct gpiod_line *line){
    
//    LOGT_DEBUG("gpiod_line_get_value()");
    return 0;

}


 
#endif
