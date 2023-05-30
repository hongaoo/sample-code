/*
 * Author: Leon.He
 * e-mail: 343005384@qq.com
 */

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

struct buffer_object {
	uint32_t width;
	uint32_t height;
	uint32_t pitch;
	uint32_t handle;
	uint32_t size;
	uint32_t *vaddr;
	uint32_t fb_id;
};

struct buffer_object buf;
struct buffer_object slave_buf;

static int modeset_create_fb(int fd, struct buffer_object *bo)
{
	struct drm_mode_create_dumb create = {};
 	struct drm_mode_map_dumb map = {};

	create.width = bo->width;
	create.height = bo->height;
	create.bpp = 32;
	drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &create);

	bo->pitch = create.pitch;
	bo->size = create.size;
	bo->handle = create.handle;
	drmModeAddFB(fd, bo->width, bo->height, 24, 32, bo->pitch,
			   bo->handle, &bo->fb_id);

	map.handle = create.handle;
	drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &map);

	bo->vaddr = mmap(0, create.size, PROT_READ | PROT_WRITE,
			MAP_SHARED, fd, map.offset);

	for(int i = 0;i<bo->size/sizeof(uint32_t);i++){
		bo->vaddr[i] = 0xff00ff00;
	}

	return 0;
}

static void modeset_destroy_fb(int fd, struct buffer_object *bo)
{
	struct drm_mode_destroy_dumb destroy = {};

	drmModeRmFB(fd, bo->fb_id);

	munmap(bo->vaddr, bo->size);

	destroy.handle = bo->handle;
	drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy);
}

bool test_prime(int master_fd,int slave_fd, struct buffer_object *master_bo, struct buffer_object *slave_bo){
	int ret, buf_fd;

	struct drm_mode_map_dumb map = {};
	drmModeClip *clip = calloc(1,sizeof(clip));


	ret = drmPrimeHandleToFD(master_fd, master_bo->handle, O_CLOEXEC, &buf_fd);
	if (ret) {
		printf("Failed to make prime FD for handle: %d\n", errno);
		return false;
	}	
	
	ret = drmPrimeFDToHandle(slave_fd, buf_fd, &slave_bo->handle);
	if (ret) {
		printf("Failed to make prime handle for FD: %d\n", errno);
		return false;
	}
	close(buf_fd);

	slave_bo->width = master_bo->width;
	slave_bo->height = master_bo->height;
	slave_bo->size = master_bo->size;

	ret = drmModeAddFB(slave_fd, master_bo->width, master_bo->height, 24, 32, master_bo->pitch, slave_bo->handle, &slave_bo->fb_id);
	if(ret){
		return false;
	}

	map.handle = slave_bo->handle;
	drmIoctl(slave_fd, DRM_IOCTL_MODE_MAP_DUMB, &map);
	if(ret){
		return false;
	}

	slave_bo->vaddr = mmap(0, master_bo->size, PROT_READ | PROT_WRITE,
			MAP_SHARED, slave_fd, map.offset);

	for(int i = 0;i<slave_bo->size/sizeof(uint32_t);i++){
		slave_bo->vaddr[i] = 0x00ff0000;
	}

	clip[0].x1 = 0;
	clip[0].y1 = 0;
	clip[0].x2 = 1280;
	clip[0].y2 = 800;
	ret = drmModeDirtyFB(slave_fd, slave_bo->fb_id, &clip[0], 1);
	if(ret){
		return false;
	}

	free(clip);

	return ret;
}

int main(int argc, char **argv)
{
	int fd, slave_fd;
	drmModeConnector *conn, *slave_conn;
	drmModeRes *res, *slave_res;
	uint32_t conn_id;
	uint32_t crtc_id;
	int ret;
	drmModeClip *clip = calloc(1,sizeof(clip));

	fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);

	res = drmModeGetResources(fd);
	crtc_id = res->crtcs[0];
	conn_id = res->connectors[0];

	conn = drmModeGetConnector(fd, conn_id);
	conn->modes[0].hdisplay = 1280;
	conn->modes[0].vdisplay = 800;

	buf.width = conn->modes[0].hdisplay;
	buf.height = conn->modes[0].vdisplay;

	modeset_create_fb(fd, &buf);

	drmModeSetCrtc(fd, crtc_id, buf.fb_id,
			0, 0, &conn_id, 1, &conn->modes[0]);



	slave_fd = open("/dev/dri/card2", O_RDWR | O_CLOEXEC);

	slave_res = drmModeGetResources(slave_fd);
	crtc_id = slave_res->crtcs[0];
	conn_id = slave_res->connectors[0];

	slave_conn = drmModeGetConnector(slave_fd, conn_id);
	// buf.width = conn->modes[0].hdisplay;
	// buf.height = conn->modes[0].vdisplay;

	test_prime(fd, slave_fd, &buf, &slave_buf);
	drmModeSetCrtc(slave_fd, crtc_id, slave_buf.fb_id, 0, 0, &conn_id, 1, &slave_conn->modes[0]);


	clip[0].x1 = 0;
	clip[0].y1 = 0;
	clip[0].x2 = 1280;
	clip[0].y2 = 800;
	for(int i = 0;i<slave_buf.size/sizeof(uint32_t);i++){
		slave_buf.vaddr[i] = 0x0000ff00;
	}
	ret = drmModeDirtyFB(slave_fd, slave_buf.fb_id, &clip[0], 1);
	if(ret){
		return false;
	}

	free(clip);

	getchar();

	modeset_destroy_fb(fd, &buf);
	modeset_destroy_fb(slave_fd,&slave_buf);

	drmModeFreeConnector(conn);
	drmModeFreeResources(res);

	close(fd);

	return 0;
}
