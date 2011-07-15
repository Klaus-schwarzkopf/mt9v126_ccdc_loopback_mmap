/* Copyright (C) 2011 Texas Instruments Incorporated - http://www.ti.com/
 *
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * Neither the name of Texas Instruments Incorporated nor the names of
 * its contributors may be used to endorse or promote products derived
 * from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *
 *       [MT9V126]--->[CCDC]--->[CCDC Video node]------>[display video node] media pipeline will be set
 *       and frames looped back.
 *
 *
 *       Based on the mc examples: http://arago-project.org/git/projects/?p=examples-davinci.git;a=summary
 *
 */

#include <stdio.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/mman.h>
#include <getopt.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <linux/videodev.h>
#include <linux/videodev2.h>
#include <media/davinci/vpfe_capture.h>
#include <linux/media.h>
#include <linux/v4l2-subdev.h>
#include <linux/fb.h>
#include "common.h"

/* Device parameters */
#define DISPLAY_DEVICE	"/dev/video5"
#define BYTESPERPIXEL	2

#define ALIGN(x, y)	(((x + (y-1))/y)*y)
#define CLEAR(x) memset (&(x), 0, sizeof (x))

#define APP_NUM_BUFS	3
/* capture buffer addresses stored here */

struct mmap_buffer {
	void *start;
	size_t length;
};

struct mmap_buffer	capture_buffers[APP_NUM_BUFS];
struct mmap_buffer	display_buffers[APP_NUM_BUFS];

#define NUM_ENTITIES	15
struct media_entity_desc	*entity[NUM_ENTITIES];
struct media_links_enum		links;
int				entities_count;
int inp_width = 640, inp_height=480;
int capture_pitch;
int cap_numbuffers = 0, disp_numbuffers =0;

v4l2_std_id standard_id = V4L2_STD_PAL;

#define CODE		V4L2_MBUS_FMT_YUYV8_2X8

int ccdc_output_fd, display_fd, media_fd;

static int xioctl(int fh, int request, void *arg) {
    int r;

    do {
        r = ioctl(fh, request, arg);
    } while (-1 == r && EINTR == errno);

    return r;
}

int reset_links()
{
	int ret,i, index;
	struct media_link_desc link;


	/* 24. de-enable all the links which are active right now */
	for(index = 0; index < entities_count; index++) {

		links.entity = entity[index]->id;

		links.pads = malloc(sizeof( struct media_pad_desc) * entity[index]->pads);
		links.links = malloc(sizeof(struct media_link_desc) * entity[index]->links);

		ret = ioctl(media_fd, MEDIA_IOC_ENUM_LINKS, &links);
		if (ret < 0) {
			if (errno == EINVAL)
				break;
		}else{

			for(i = 0;i< entity[index]->links; i++)
			{		/* go through each active link */
				       if(links.links->flags & MEDIA_LNK_FL_ENABLED) {
					        /* de-enable the link */
					        memset(&link, 0, sizeof(link));

						link.flags |=  ~MEDIA_LNK_FL_ENABLED;
						link.source.entity = links.links->source.entity;
						link.source.index = links.links->source.index;
						link.source.flags = MEDIA_PAD_FL_OUTPUT;

						link.sink.entity = links.links->sink.entity;
						link.sink.index = links.links->sink.index;
						link.sink.flags = MEDIA_PAD_FL_INPUT;

						ret = ioctl(media_fd, MEDIA_IOC_SETUP_LINK, &link);
						if(ret) {
							printf("failed to de-enable link \n");
						}

				       }

				links.links++;
			}
		}
	}

	return 0;
}

int show_links()
{
	int ret=0, i=0, index;


	/* 5.enumerate links for each entity */
	printf("5.enumerating links/pads for entities\n");
	for(index = 0; index < entities_count; index++) {

		links.entity = entity[index]->id;

		links.pads = malloc(sizeof( struct media_pad_desc) * entity[index]->pads);
		links.links = malloc(sizeof(struct media_link_desc) * entity[index]->links);

		ret = ioctl(media_fd, MEDIA_IOC_ENUM_LINKS, &links);
		if (ret < 0) {
			if (errno == EINVAL)
				break;
		}else{
			/* display pads info first */
			if(entity[index]->pads)
				printf("pads for entity %x=", entity[index]->id);

			for(i = 0;i< entity[index]->pads; i++)
			{
				printf("(%x, %s) ", links.pads->index,(links.pads->flags & MEDIA_PAD_FL_INPUT)?"INPUT":"OUTPUT");
				links.pads++;
			}

			printf("\n");

			/* display links now */
			for(i = 0;i< entity[index]->links; i++)
			{
				printf("[%x:%x]-------------->[%x:%x]",links.links->source.entity,
				       links.links->source.index,links.links->sink.entity,links.links->sink.index);
				       if(links.links->flags & MEDIA_LNK_FL_ENABLED)
						printf("\tACTIVE\n");
				       else
						printf("\tINACTIVE \n");

				links.links++;
			}

			printf("\n");
		}
	}

	printf("**********************************************\n");

	return 0;
}

int show_entities()
{
	int E_VIDEO;
	int E_MT9V126;
	int E_CCDC;
	int i =0,  ret, index;
	struct media_link_desc link;

	for (i=0;i< NUM_ENTITIES;i++){
		entity[i] = (struct media_entity_desc *)malloc(sizeof(struct media_entity_desc));
	}


	/* 4.enumerate media-entities */
	printf("4.enumerating media entities\n");
	index = 0;
	do {
		memset(entity[index], 0, sizeof(*entity[0]));
		entity[index]->id = index | MEDIA_ENT_ID_FLAG_NEXT;

		ret = ioctl(media_fd, MEDIA_IOC_ENUM_ENTITIES, entity[index]);
		if (ret < 0) {
			if (errno == EINVAL)
				break;
		}else {
			if (!strcmp(entity[index]->name, E_VIDEO_CCDC_OUT_NAME)) {
				E_VIDEO =  entity[index]->id;
			}
			else if (!strcmp(entity[index]->name, E_MT9V126_NAME)) {
				E_MT9V126 =  entity[index]->id;
			}
			else if (!strcmp(entity[index]->name, E_CCDC_NAME)) {
				E_CCDC =  entity[index]->id;
			}
			printf("[%x]:%2.i %i %s\n",entity[index]->id,  entity[index]->v4l.major, entity[index]->v4l.minor,  entity[index]->name);
		}

		index++;
	}while(ret == 0);
	entities_count = index;
	printf("total number of entities: %x\n", entities_count);
	printf("**********************************************\n");

	//show_links(media_fd);

	/* 6. enable 'MT9V126-->ccdc' link */
	printf("6. ENABLEing link [MT9V126]----------->[ccdc]\n");
	memset(&link, 0, sizeof(link));

	link.flags |=  MEDIA_LNK_FL_ENABLED;
	link.source.entity = E_MT9V126;
	link.source.index = P_MT9V126;
	link.source.flags = MEDIA_PAD_FL_OUTPUT;

	link.sink.entity = E_CCDC;
	link.sink.index = P_CCDC_SINK;
	link.sink.flags = MEDIA_PAD_FL_INPUT;

	ret = ioctl(media_fd, MEDIA_IOC_SETUP_LINK, &link);
	if(ret < 0) {
		printf("failed to enable link between MT9V126 and ccdc\n");
		return ret;
	} else
		printf("[MT9V126]----------->[ccdc]\tENABLED\n");

	/* 7. enable 'ccdc->memory' link */
	printf("7. ENABLEing link [ccdc]----------->[video_node]\n");
	memset(&link, 0, sizeof(link));

	link.flags |=  MEDIA_LNK_FL_ENABLED;
	link.source.entity = E_CCDC;
	link.source.index = P_CCDC_SOURCE;
	link.source.flags = MEDIA_PAD_FL_OUTPUT;

	link.sink.entity = E_VIDEO;
	link.sink.index = P_VIDEO;
	link.sink.flags = MEDIA_PAD_FL_INPUT;

	ret = ioctl(media_fd, MEDIA_IOC_SETUP_LINK, &link);
	if(ret < 0) {
		printf("failed to enable link between ccdc and video node\n");
		return ret;
	} else
		printf("[ccdc]----------->[video_node]\t ENABLED\n");

	printf("**********************************************\n");

	return 0;
}

int setup_camera()
{
	struct v4l2_subdev_format fmt;
	int mt9v126_fd, ret;
	/* 8. set format on pad of MT9V126 */
	mt9v126_fd = open("/dev/v4l-subdev0",  O_RDWR);
	if(mt9v126_fd == -1) {
		printf("failed to open %s\n", "/dev/v4l-subdev0");
		return mt9v126_fd;
	}

	printf("8. setting format on pad of MT9V126 entity. . .\n");
	memset(&fmt, 0, sizeof(fmt));

	fmt.pad = P_MT9V126;
	fmt.which = V4L2_SUBDEV_FORMAT_ACTIVE;
	fmt.format.code = CODE;
	fmt.format.width = inp_width;
	fmt.format.height = inp_height;
	fmt.format.colorspace = V4L2_COLORSPACE_SMPTE170M;
	fmt.format.field = V4L2_FIELD_INTERLACED;

	ret = ioctl(mt9v126_fd, VIDIOC_SUBDEV_S_FMT, &fmt);
	if(ret) {
		printf("failed to set format on pad %x\n", fmt.pad);
		return ret;
	}
	else
		printf("successfully format is set on pad %x\n", fmt.pad);
	if(mt9v126_fd) {
		close(mt9v126_fd);
		printf("closed mt9v126 sub-device\n");
	}
	return 0;
}

int setup_ccdc()
{
	struct v4l2_subdev_format fmt;
	int ccdc_fd, ret;
	/* 9. set format on sink-pad of ccdc */
	ccdc_fd = open("/dev/v4l-subdev1", O_RDWR);
	if(ccdc_fd == -1) {
		printf("failed to open %s\n", "/dev/v4l-subdev1");
		return ccdc_fd;
	}

	printf("9. setting format on sink-pad of ccdc entity. . .\n");
	memset(&fmt, 0, sizeof(fmt));

	fmt.pad = P_CCDC_SINK;
	fmt.which = V4L2_SUBDEV_FORMAT_ACTIVE;
	fmt.format.code = CODE;
	fmt.format.width = inp_width;
	fmt.format.height = inp_height;
	fmt.format.colorspace = V4L2_COLORSPACE_SMPTE170M;
	fmt.format.field = V4L2_FIELD_INTERLACED;

	ret = ioctl(ccdc_fd, VIDIOC_SUBDEV_S_FMT, &fmt);
	if(ret) {
		printf("failed to set format on pad %x\n", fmt.pad);
		return ret;
	}
	else
		printf("successfully format is set on pad %x\n", fmt.pad);

	/* 10. set format on OF-pad of ccdc */
	printf("10. setting format on OF-pad of ccdc entity. . . \n");
	memset(&fmt, 0, sizeof(fmt));

	fmt.pad = P_CCDC_SOURCE;
	fmt.which = V4L2_SUBDEV_FORMAT_ACTIVE;
	fmt.format.code = CODE;
	fmt.format.width = inp_width;
	fmt.format.height = inp_height;
	fmt.format.colorspace = V4L2_COLORSPACE_SMPTE170M;
	fmt.format.field = V4L2_FIELD_INTERLACED;

	ret = ioctl(ccdc_fd, VIDIOC_SUBDEV_S_FMT, &fmt);
	if(ret) {
		printf("failed to set format on pad %x\n", fmt.pad);
		return ret;
	}
	else
		printf("successfully format is set on pad %x\n", fmt.pad);

	printf("**********************************************\n");
	if(ccdc_fd) {
		close(ccdc_fd);
		printf("closed ccdc sub-device\n");
	}
	return 0;
}

int setup_ccdc_output()
{
	int  index, ret, i;
	struct v4l2_input input;
	struct v4l2_format v4l2_fmt;
	struct v4l2_requestbuffers req;
	struct v4l2_buffer in_querybuf;
	struct v4l2_capability cap;


	ret = xioctl(ccdc_output_fd, VIDIOC_S_INPUT, &index);
	if (ret) {

		close(display_fd);
		printf("VIDIOC_S_INPUT\n");
	}

	if (-1 == xioctl(ccdc_output_fd, VIDIOC_QUERYCAP, &cap)) {
		if (EINVAL == errno) {
			fprintf(stderr, "Display is no V4L2 device\n");
			exit(EXIT_FAILURE);
		} else {
			printf("VIDIOC_QUERYCAP");
		}
	}

	if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
		fprintf(stderr, "ccdc_output is no video capture device\n");
		exit(EXIT_FAILURE);
	}
	fprintf(stderr, "ccdc_output is video capture device\n");

	if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
		fprintf(stderr, "ccdc_output does not support streaming i/o\n");
		exit(EXIT_FAILURE);
	}
	fprintf(stderr, "ccdc_output does support streaming i/o\n");


	/* 12.enumerate inputs supported by capture via mt9v126[wh s active through mc] */
	printf("enumerating INPUTS\n");
	bzero(&input, sizeof(struct v4l2_input));
	input.type = V4L2_INPUT_TYPE_CAMERA;
	input.index = 0;
	index = 0;
  	while (1) {

		ret = ioctl(ccdc_output_fd, VIDIOC_ENUMINPUT, &input);
		if(ret != 0)
			break;

		printf("[%x].%s\n", index, input.name);

		bzero(&input, sizeof(struct v4l2_input));
		index++;
		input.index = index;
  	}
  	printf("**********************************************\n");

	/* 13.setting Camera input */
	printf("setting camera input. . .\n");
	bzero(&input, sizeof(struct v4l2_input));
	input.type = V4L2_INPUT_TYPE_CAMERA;
	input.index = 0;
	ret = ioctl (ccdc_output_fd, VIDIOC_S_INPUT, &input.index);
	if (-1 == ret) {
		printf("failed to set camera with capture device\n");
		return ret;
	} else
		printf("successfully set camera input\n");

	printf("**********************************************\n");


	struct v4l2_cropcap cropcap;
    struct v4l2_crop crop;
    struct v4l2_format fmt;
    struct v4l2_fmtdesc fmt_desc;
    CLEAR(fmt_desc);
    fmt_desc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fprintf(stderr, "\nAvailable image formats at the capture driver :-\n");
    if (0 == ioctl(ccdc_output_fd, VIDIOC_ENUM_FMT, &fmt_desc)) {
        fprintf(stderr, "\tfmt_desc.index = %d\n", fmt_desc.index);
        fprintf(stderr, "\tfmt_desc.type = %d\n", fmt_desc.type);
        fprintf(stderr, "\tfmt_desc.description = %s\n", fmt_desc.description);
        fprintf(stderr, "\tfmt_desc.pixelformat = %x\n\n", fmt_desc.pixelformat);

    }

    CLEAR(fmt);

    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	fmt.fmt.pix.width = 640;
	fmt.fmt.pix.height = 480;
	fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_UYVY;
	fmt.fmt.pix.field = V4L2_FIELD_INTERLACED;

	fprintf(stderr, "\nForce Format: %x\n", fmt.fmt.pix.pixelformat);
	if (-1 == ioctl(ccdc_output_fd, VIDIOC_S_FMT, &fmt))
		printf("VIDIOC_S_FMT");


    fprintf(stderr, "\nSelected image format at the capture driver :-\n");
    fprintf(stderr, "\tfmt.fmt.pix.bytesperline = %d\n", fmt.fmt.pix.bytesperline);
    fprintf(stderr, "\tfmt.fmt.pix.width = %d\n", fmt.fmt.pix.width);
    fprintf(stderr, "\tfmt.fmt.pix.height = %d\n", fmt.fmt.pix.height);
    fprintf(stderr, "\tfmt.fmt.pix.pixelformat = %x\n\n", fmt.fmt.pix.pixelformat);


    /* Select video input, video standard and tune here. */


    CLEAR(cropcap);

    cropcap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (0 == ioctl(ccdc_output_fd, VIDIOC_CROPCAP, &cropcap)) {
        crop.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        crop.c = cropcap.defrect; /* reset to default */


        //for calibration
        crop.c.top=120;
        crop.c.left=160;

        crop.c.width = 320;
        crop.c.height = 240;

        if (-1 == ioctl(ccdc_output_fd, VIDIOC_S_CROP, &crop)) {
            switch (errno) {
                case EINVAL:
                    /* Cropping not supported. */
                    break;
                default:
                    /* Errors ignored. */
                    break;
            }
        }
    } else {
        /* Errors ignored. */
    	printf("VIDIOC_CROPCAP\n");
    }





	/* 15.setting format */
	printf("setting format V4L2_PIX_FMT_UYVY\n");
	CLEAR(v4l2_fmt);
	v4l2_fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	v4l2_fmt.fmt.pix.width = inp_width;
	v4l2_fmt.fmt.pix.height = inp_height;
	v4l2_fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_UYVY;
	v4l2_fmt.fmt.pix.field = V4L2_FIELD_INTERLACED;

	ret =ioctl(ccdc_output_fd, VIDIOC_S_FMT, &v4l2_fmt);
	if (-1 == ret) {
		printf("failed to set format on capture device \n");
		return ret;
	} else
		printf("successfully set the format on capture device\n");

	/* 16.call G_FMT for knowing picth */
	ret = ioctl(ccdc_output_fd, VIDIOC_G_FMT, &v4l2_fmt);
	if (-1 == ret) {
		printf("failed to get format from capture device \n");
		return ret;
	} else {
		printf("capture_pitch: %x\n", v4l2_fmt.fmt.pix.bytesperline);
		capture_pitch = v4l2_fmt.fmt.pix.bytesperline;
	}

	printf("**********************************************\n");



	/* 17.make sure 3 buffers are supported for streaming */
	CLEAR(req);
	req.count = APP_NUM_BUFS;
	req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	req.memory = V4L2_MEMORY_MMAP;

	ret =ioctl(ccdc_output_fd, VIDIOC_REQBUFS, &req);
	if (-1 == ret) {
		printf("call to VIDIOC_REQBUFS failed\n");
		return ret;
	}

	if (req.count != APP_NUM_BUFS) {
		printf("%d buffers not supported by capture device\n", APP_NUM_BUFS);
		printf("%d buffers supported\n", req.count);
	} else
		printf("%d buffers are supported for streaming (capture)\n", req.count);
	cap_numbuffers = req.count;
	printf("**********************************************\n");

	/*calling ioctl QUERYBUF and MMAP ON CAPTURE*/
	printf("Querying capture buffers and MMAPing capture\n");
	for(i = 0;  i < cap_numbuffers; i++) {
		in_querybuf.index = i;
		in_querybuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		in_querybuf.memory = V4L2_MEMORY_MMAP;
		ret = ioctl(ccdc_output_fd, VIDIOC_QUERYBUF, &in_querybuf);
		if(ret < 0) {
			printf("capture QUERYBUF failed\n");
			exit(0);
		}
		printf("buffer %d queried \n", i);
		capture_buffers[i].length = in_querybuf.length;
		printf("buf length : %d\n",in_querybuf.length);
		capture_buffers[i].start = (char*) mmap(NULL, in_querybuf.length,
							      PROT_READ | PROT_WRITE,
							      MAP_SHARED, ccdc_output_fd,
							      in_querybuf.m.offset);
		if((void*)capture_buffers[i].start == (void*) -1) {
			printf("capture MMAP failed\n");
			exit(0);
		}
		printf("Capture Buffer %u mapped at address %p.\n",i,
		       capture_buffers[i].start);
	}

	return 0;

}

int setup_display()
{
	int  ret, i;
	struct v4l2_output output;
	struct v4l2_format format;
	struct v4l2_requestbuffers out_reqbuff;
	struct v4l2_buffer out_querybuf;
	int disppitch, dispheight, dispwidth;
	struct v4l2_fmtdesc fmt_desc;
	struct v4l2_capability cap;
	struct v4l2_format fmt;

	printf("**********************************************\n");
	if (-1 == xioctl(display_fd, VIDIOC_QUERYCAP, &cap)) {
		if (EINVAL == errno) {
			fprintf(stderr, "is no V4L2 device\n");
			exit(EXIT_FAILURE);
		} else {
			printf("VIDIOC_QUERYCAP");
		}
	}

	if (!(cap.capabilities & V4L2_CAP_VIDEO_OUTPUT)) {
		fprintf(stderr, "is no video capture device\n");
		exit(EXIT_FAILURE);
	}


	if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
		fprintf(stderr, "does not support streaming i/o\n");
		exit(EXIT_FAILURE);
	}





	/* 13.setting COMPOSITE output */
	printf("setting Display output. . . \n");
	bzero(&output, sizeof(struct v4l2_output));
	output.index = 0;
	ret = ioctl (display_fd, VIDIOC_S_OUTPUT, &output.index);
	if (-1 == ret) {
		printf("failed to set Display device\n");
		return ret;
	} else
		printf("successfully set Display display\n");
//TODO
	/* Set Standard for display ------ */
//	printf("set STD on display\n");
//	ret = ioctl(display_fd, VIDIOC_S_STD, &standard_id);
//	if(ret < 0) {
//			printf("display S_STD failed\n");
//			return ret;
//	}
//	printf("capture S_STD successful\n");
//	printf("standard after s_std is : %x\n",(unsigned int)standard_id);


	//only index 0
    CLEAR(fmt_desc);
	fmt_desc.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	fprintf(stderr, "Available image formats at the display driver :-\n");
	if (0 == ioctl(display_fd, VIDIOC_ENUM_FMT, &fmt_desc)) {
		fprintf(stderr, "\tfmt_desc.index = %d\n", fmt_desc.index);
		fprintf(stderr, "\tfmt_desc.type = %d\n", fmt_desc.type);
		fprintf(stderr, "\tfmt_desc.description = %s\n", fmt_desc.description);
		fprintf(stderr, "\tfmt_desc.pixelformat = %x\n", fmt_desc.pixelformat);
	}


	/*calling S_FMT on display*/
	printf("Test S_FMT display\n");
	bzero((void *)&format,sizeof( format));
	format.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	format.fmt.pix.width = 320,//inp_width;
	format.fmt.pix.height = 240,//inp_height;
	format.fmt.pix.pixelformat = V4L2_PIX_FMT_UYVY;
	//format.fmt.pix.field = V4L2_FIELD_NONE;
	ret = ioctl(display_fd, VIDIOC_S_FMT, &format);
	if(ret) {
		printf("display S_FMT failed\n");
		return ret;
	}

	printf("display S_FMT successful\n");


	printf("size image : %d\n",format.fmt.pix.sizeimage);

	/*calling G_FMT on display*/
	printf("Test G_FMT display\n");
	bzero((void *)&format,sizeof( format));
	format.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	ret = ioctl(display_fd, VIDIOC_G_FMT, &format);
	if(ret < 0) {
		printf("display G_FMT failed\n");
		exit(0);
	}
	printf("display G_FMT successful\n");




	    /*
	     * It is necessary for applications to know about the
	     * buffer chacteristics that are set by the driver for
	     * proper handling of buffers
	     * These are : width,height,pitch and image size
	     */
	    fprintf(stderr, "3. Test GetFormat\n");
	    fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;

	    if (ioctl(display_fd, VIDIOC_G_FMT, &fmt) < 0) {
	        fprintf(stderr, "\tError: Get Format failed: VID1\n");
	        return -1;
	    }

	    dispheight = fmt.fmt.pix.height;
	    disppitch = fmt.fmt.pix.bytesperline;
	    dispwidth = fmt.fmt.pix.width;

	    fprintf(stderr, "\tdispheight = %d\n\tdisppitch = %d\n\tdispwidth = %d\n",
	            dispheight, disppitch, dispwidth);
	    fprintf(stderr, "\timagesize = %d, field %i\n", fmt.fmt.pix.sizeimage, fmt.fmt.pix.field);


	/*calling ioctl REQBUF on display*/
	printf("Test REQBUF display\n");
	out_reqbuff.count = cap_numbuffers;
	out_reqbuff.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	out_reqbuff.memory = V4L2_MEMORY_MMAP;

	ret = ioctl(display_fd, VIDIOC_REQBUFS, &out_reqbuff);
	if (out_reqbuff.count != cap_numbuffers) {
		printf("%d buffers not supported by capture device\n", cap_numbuffers);
		printf("%d buffers supported\n", out_reqbuff.count);
		return -1;
	} else
		printf("%d buffers are supported for streaming (display)\n", out_reqbuff.count);

	disp_numbuffers = out_reqbuff.count;
	printf("**********************************************\n");
	/*calling ioctl QUERYBUF and MMAP on display*/
	printf("Querying display buffers and MMAPing \n");
	for(i = 0;  i < disp_numbuffers; i++) {
		out_querybuf.index = i;
		out_querybuf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
		out_querybuf.memory = V4L2_MEMORY_MMAP;

		ret = ioctl(display_fd, VIDIOC_QUERYBUF, &out_querybuf);
		if(ret < 0) {
			printf("display QUERYBUF failed\n");
			exit(0);
		}
		printf("buffer %d queried \n", i);
		display_buffers[i].length = out_querybuf.length;
		printf("buf length : %d\n",out_querybuf.length);
		display_buffers[i].start = (char*) mmap(NULL, out_querybuf.length,
							      PROT_READ | PROT_WRITE,
							      MAP_SHARED, display_fd,
							      out_querybuf.m.offset);
		if((void*)display_buffers[i].start == (void*) -1) {
			printf("Display MMAP failed\n");
			exit(0);
		}
		printf("Display Buffer %u mapped at address %p.\n",i,
		       display_buffers[i].start);
	}
	printf("**********************************************\n");
	return 0;
}

int main(int argc, char *argv[])
{

	int i, ret,  frame_count;


	enum v4l2_buf_type type;
	struct v4l2_buffer cap_buf;

	char *src, *dst;
	int display_pitch = 0;



	struct v4l2_buffer out_qbuf,disp_qbuf;


	int buf_size = ALIGN((inp_width*inp_height*2), 4096);
	/* defaults */
	//int inp_width = 720, inp_height = 480;

	/* 3.open media device */
	media_fd = open("/dev/media0", O_RDWR);
	if (media_fd < 0) {
		printf("%s: Can't open media device %s\n", __func__, "/dev/media0");
		exit(0);
	}
	printf("media device opened\n");


	if(show_entities()< 0)
		goto cleanup;

	if(show_links()< 0)
		goto cleanup;
	/* 11.open capture device */
	if ((ccdc_output_fd = open("/dev/video0", O_RDWR | O_NONBLOCK, 0)) <= -1) {
		printf("failed to open %s \n", "/dev/video0");
		exit(0);
	}
	printf("ccdc device opened\n");


	display_fd = open(DISPLAY_DEVICE, O_RDWR);
	if(display_fd < 0) {
		printf("Failed to open DISPLAY_DEVICE device\n");
		exit(0);
	}
	printf("display device opened\n");

	if(setup_camera()< 0)
		goto cleanup;

	if(setup_ccdc()< 0)
		goto cleanup;

	if(setup_ccdc_output()< 0)
		goto cleanup;

	//TODO: set the correct standard

//	/* 14.setting std */
//	printf("setting std. . .\n");
//	if (-1 == ioctl(capt_fd, VIDIOC_S_STD, &standard_id)) {
//		printf("failed to set std on capture device\n");
//		goto cleanup;
//	} else {
//		printf("successfully std is set\n");
//	}
//
//	printf("**********************************************\n");



	if(setup_display()< 0)
		goto cleanup;


	/* 18.queue the capture buffers*/
	for (i = 0; i < cap_numbuffers; i++) {
		CLEAR(out_qbuf);
		out_qbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		out_qbuf.memory = V4L2_MEMORY_MMAP;
		out_qbuf.index = i;
		out_qbuf.length = buf_size;
		printf("Q capture BUF %x---%x---%x\n",i,(unsigned int)capture_buffers[i].start,capture_buffers[i].length );

		if (-1 == ioctl(ccdc_output_fd, VIDIOC_QBUF, &out_qbuf)) {
			printf("capture:call to VIDIOC_QBUF failed\n");
			goto cleanup;
		}
	}
	/* 18.queue the display buffers */
	for(i= 0; i < disp_numbuffers; i++) {
		CLEAR(disp_qbuf);
		disp_qbuf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
		disp_qbuf.memory = V4L2_MEMORY_MMAP;
		disp_qbuf.index = i;
		disp_qbuf.length = buf_size;

		printf("Q display BUF %x---%x---%x\n",i,(unsigned int)display_buffers[i].start,(display_buffers[i].length) );

		if (-1 == ioctl(display_fd, VIDIOC_QBUF, &disp_qbuf)) {
			printf("display:call to VIDIOC_QBUF failed\n");
			goto cleanup;
		}
	}

	/* 19.start streaming */
	CLEAR(type);
	type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (-1 == ioctl(ccdc_output_fd, VIDIOC_STREAMON, &type)) {
		printf("failed to start streaming on capture device");
		goto cleanup;
	} else
		printf("streaming started successfully\n");

	/*calling streamon on display*/
	printf("Test STREAMON display\n");
	CLEAR(type);
	type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	ret = ioctl(display_fd, VIDIOC_STREAMON, &type);
	if(ret < 0) {
		printf("display STREAMON failed\n");
		exit(0);
	}
	printf("display STREAMON successful\n");

	capture_pitch = inp_width * BYTESPERPIXEL;
	display_pitch = inp_width * BYTESPERPIXEL;

	/* 20.get 20 frames from capture device and store in a file */
	frame_count = 0;
	fprintf(stderr, "Start while...\n");
	while(frame_count < 200) {
		frame_count++;

		fprintf(stderr, "Start while... %i\n", frame_count);
		CLEAR(cap_buf);
		CLEAR(disp_qbuf);

		cap_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		cap_buf.memory = V4L2_MEMORY_MMAP;
//	     fd_set fds;
//	        struct timeval tv;
//	        int r;
//
//
//	        FD_ZERO(&fds);
//	        FD_SET(ccdc_output_fd, &fds);
//
//	        /* Timeout */
//	        tv.tv_sec = 2;
//	        tv.tv_usec = 0;
//	        r = select(ccdc_output_fd + 1, &fds, NULL, NULL, &tv);
//	        if (-1 == r) {
//	            if (EINTR == errno)
//	                continue;
//	            fprintf(stderr, "StartCameraCapture:select\n");
//	            return -1;
//	        }
//	        if (0 == r)
//	            continue;
try_again:
	//fprintf(stderr, "try...%i\n", frame_count);
		ret = ioctl(ccdc_output_fd, VIDIOC_DQBUF, &cap_buf);
		if (ret < 0) {
			if (errno == EAGAIN) {
				goto try_again;
			}
			printf("failed to DQ buffer from capture device\n");
			goto cleanup;
		}
		printf("DQ buffer from capture device\n");

		disp_qbuf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
		disp_qbuf.memory = V4L2_MEMORY_MMAP;

		ret = ioctl(display_fd, VIDIOC_DQBUF, &disp_qbuf);
		if(ret < 0) {
			printf("display DQBUF failed\n");
			exit(0);
		}

		printf("display DQBUF\n");
		src = capture_buffers[cap_buf.index].start;
		dst = display_buffers[disp_qbuf.index].start;

		for(i=0 ; i < inp_height; i++) {
			memcpy(dst, src, display_pitch);
			src += capture_pitch;
			dst += display_pitch;
		}

		//write out test image
		if(100== frame_count)
		{
			  FILE * pFile;

			  pFile = fopen ( "/test.yuv" , "wb" );
			fwrite(capture_buffers[cap_buf.index].start, capture_buffers[cap_buf.index].length, 1, pFile);
			  fclose(pFile);
		}

		/* Q the buffer for capture, again */
		cap_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		cap_buf.memory = V4L2_MEMORY_MMAP;
		ret = ioctl(ccdc_output_fd, VIDIOC_QBUF, &cap_buf);
		if (ret < 0) {
			printf("failed to Q buffer onto capture device\n");
			goto cleanup;
		}

		disp_qbuf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
		disp_qbuf.memory = V4L2_MEMORY_MMAP;
		ret = ioctl(display_fd, VIDIOC_QBUF, &disp_qbuf);
		if(ret < 0) {
			printf("display QBUF failed\n");
			printf("errno : %d\n",errno);
			exit(0);
		}
		printf(" frame #%d done\n", frame_count);
	}

	printf("**********************************************\n");
	/* 21. do stream off */
	CLEAR(type);
	type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (-1 == ioctl(ccdc_output_fd, VIDIOC_STREAMOFF, &type)) {
		printf("failed to stop streaming on capture device");
		goto cleanup;
	} else
		printf("streaming stopped successfully\n");

	/*calling STREAMOFF on display*/
	printf("Test STREAMOFF display\n");
	CLEAR(type);
	type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	ret = ioctl(display_fd, VIDIOC_STREAMOFF, &type);
	if(ret < 0) {
		printf("display STREAMOFF failed\n");
		exit(0);
	}
	printf("display STREAMOFF successful\n");
cleanup:

	/* unmap buffers */

	/* unmapping mmapped buffers */
	for(i=0;i<cap_numbuffers;i++)
		munmap(capture_buffers[i].start, capture_buffers[i].length);

	printf("unmapped capture buffers\n");

	for(i=0;i<disp_numbuffers;i++)
		munmap(display_buffers[i].start, display_buffers[i].length);

	printf("unmapped display buffers\n");

	/* 25.close all the file descriptors */
	printf("closing all the file descriptors. . .\n");
	if(ccdc_output_fd) {
		close(ccdc_output_fd);
		printf("closed capture device\n");
	}


	if(media_fd) {
		close(media_fd);
		printf("closed  media device\n");
	}
	if(display_fd) {
		close(display_fd);
		printf("closed display device\n");
	}
	return ret;
}
