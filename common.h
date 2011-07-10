#ifndef COMMON_H
#define COMMON_H

/* mc related */

/* Media entity names */
#define E_VIDEO_CCDC_OUT_NAME	"DAVINCI VIDEO CCDC output"
#define E_VIDEO_PRV_OUT_NAME	"DAVINCI VIDEO PRV output"
#define E_VIDEO_PRV_IN_NAME	"DAVINCI VIDEO PRV input"
#define E_VIDEO_RSZ_OUT_NAME	"DAVINCI VIDEO RSZ output"
#define E_VIDEO_RSZ_IN_NAME	"DAVINCI VIDEO RSZ input"
#define E_TVP514X_NAME		"tvp514x"
#define E_TVP7002_NAME		"tvp7002"
#define E_MT9P031_NAME		"mt9p031"
#define E_MT9V126_NAME		"mt9v126"
#define E_CCDC_NAME		"DAVINCI CCDC"
#define E_PRV_NAME		"DAVINCI PREVIEWER"
#define E_RSZ_NAME		"DAVINCI RESIZER"
#define E_AEW_NAME		"DAVINCI AEW"
#define E_AF_NAME		"DAVINCI AF"

/* pad id's as enumerated by media device*/
#define P_RSZ_SINK	0 /* sink pad of rsz */
#define P_RSZ_SOURCE	1 /* source pad of rsz */
#define P_PRV_SINK	0
#define P_PRV_SOURCE	1
#define P_RSZ_VID_OUT	0 /* only one pad for video node */
#define P_RSZ_VID_IN	0 /* only one pad for video node */
#define P_PRV_VID_IN	0
#define P_PRV_VID_OUT	0
#define P_TVP514X	0 /* only one pad for decoder */
#define P_TVP7002	0 /* only one pad for decoder */
#define P_MT9P031	0 /* only one pad for sensor */
#define P_MT9V126	0 /* only one pad for sensor */
#define P_CCDC_SINK	0 /* sink pad of ccdc */
#define P_CCDC_SOURCE	1 /* source pad which connects video node */
#define P_VIDEO		0 /* only one input pad for video node */
#define P_AEW		0
#define P_AF		0


#endif
