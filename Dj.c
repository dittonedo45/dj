/* Last Edited on: 09/05/2021 07:40:42 (Sunday (129/09) May 2021) */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>
#include <regex.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/log.h>
#include <fcntl.h>
#include <getopt.h>
#include <ruby/ruby.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
#include <assert.h>
#include <pthread.h>


VALUE to_t;
AVFormatContext *o_ctx;
AVCodecContext *encoder;
SwrContext *swr;
pthread_mutex_t mmutex;
pthread_mutex_t rbutex;

static time_t Time ( time_t * Tt )
{
        return time ( Tt );
}

static int Random ( a, b )
int a, b;
{
#define min(a,b) (a>b?b:a)
        return ( rand (  ) % abs ( b - a ) ) + min ( a, b );
}


static VALUE
dj_load_tracks (VALUE arg, VALUE oparg, int argsc, const VALUE *args)
{

	do {
		if (rb_funcall (arg, rb_intern ("=="), 1, rb_str_new_cstr (".."))==Qtrue |
		rb_funcall (arg, rb_intern ("=="), 1, rb_str_new_cstr ("."))==Qtrue)
			break;
		//arg=rb_funcall (rb_cFile, rb_intern ("join"), 2, oparg, arg);

		rb_funcall (to_t, rb_intern ("push"), 1,  arg);
	} while (0);

		return(Qnil);
}

static void
ReadDir (char *DirName)
{
		VALUE temp_var;

	pthread_mutex_lock (&rbutex);
		do {
		temp_var = rb_funcall (rb_cFile, rb_intern ("directory?"), 1,
				rb_str_new_cstr (DirName));

		if (temp_var!=Qtrue) break;

		temp_var=
		rb_funcall (rb_cDir,


		rb_intern ("[]"),
		1, rb_funcall (rb_cFile,
		rb_intern ("join"), 2,
		rb_str_new_cstr (DirName),
		rb_str_new_cstr ("*.mp3")));

		if (rb_funcall (temp_var, rb_intern ("empty?"), 0)==Qtrue)
			break;

		rb_block_call (temp_var, rb_intern ("each"),
				0, 0, dj_load_tracks, rb_str_new_cstr (DirName));
		} while (0);
	pthread_mutex_unlock (&rbutex);

		
		return;
}

static void dj_show_error ()
{
	;;
	rb_set_errinfo (Qnil);
}

static VALUE dj_safe (VALUE v)
{
	int (*mmain) (int, char**);
	intptr_t **argss=(intptr_t**)v;
	int argsc = *(int*)argss[0];
	char **args = *(char***)argss[1];

	mmain = (int(*)(int, char**)) argss[2];

	mmain (argsc, args);
	return (Qnil);

}

static void dj_run_safe (int (*dj_main) (int, char**), int argsc, char **args)
{
	int s=0;
	intptr_t *argss[3]={
		&argsc,
		&args,
		dj_main
	};
	rb_protect (dj_safe, (VALUE)argss, &s);
	if (s) { dj_show_error (); }
}
static int my_player(void *d, uint8_t * b, int l)
{
	fwrite (b, l, 1, stdout), fflush (stdout);
	return(1);
}

static int dj_init_output (char *ot)
{
	const AVCodec *enc;
	int ret;

	ret = avformat_alloc_output_context2 (&o_ctx, NULL, ot, NULL);

	if (!o_ctx || ret<0) return (0);

	enc=avcodec_find_encoder (o_ctx->oformat->audio_codec);

	if (!enc) return(0);

	encoder = avcodec_alloc_context3 (enc);
	if (!encoder) return (0);

	encoder->sample_fmt = enc->sample_fmts [0];
	encoder->sample_rate = enc->supported_samplerates ? enc->supported_samplerates[0]:
		44100;
	encoder->channels=2;
	encoder->channel_layout = AV_CH_LAYOUT_STEREO;

	if (avcodec_open2 (encoder, enc, 0)<0) return (0);
	AVStream *stream = avformat_new_stream (o_ctx, enc);
	avcodec_parameters_from_context(stream->codecpar, encoder);
	o_ctx->pb =
		avio_alloc_context(av_malloc(1054), 1054, 1, 0, 0,
				   my_player, 0);
	ret = avformat_write_header(o_ctx, 00);
	assert(ret>=0);

	return(1);
}
void dj_finalize ();

void dj_init ()
{
	pthread_mutex_init (&mmutex, NULL);
	pthread_mutex_init (&rbutex, NULL);

		assert (dj_init_output ("mp3"));
		atexit (dj_finalize);
		ruby_init ();
		ruby_init_loadpath ();
}
static void decode_my_frame (AVFrame *frame)
{
	AVFrame *ff = av_frame_alloc ();
	int ret;
	AVPacket *pkt = av_packet_alloc ();

	ff->sample_rate = encoder->sample_rate;
	ff->channel_layout = encoder->channel_layout;
	ff->channels = encoder->channels;
	ff->nb_samples = encoder->frame_size;
	ff->format = encoder->sample_fmt;

	av_frame_get_buffer (ff, 0);

	ret = swr_convert_frame (swr, ff, frame);
	assert (ret >= 0);
	do {
		ret = avcodec_send_frame (encoder, ff);
		if (ret<0) break;
		ret = avcodec_receive_packet (encoder, pkt);
		if (ret<0) break;
		pkt->stream_index=0;
		av_interleaved_write_frame (o_ctx, pkt);
	} while (0);
	av_packet_free (&pkt);
}
static void clear_convertor ();

static
void init_convertor (AVCodecContext *enc, AVCodecContext *dec)
{
	if (swr) clear_convertor ();
	swr = swr_alloc_set_opts (0,
			enc->channel_layout,
			enc->sample_fmt,
			enc->sample_rate,

		dec->channel_layout,
		dec->sample_fmt,
		dec->sample_rate,
		0, 0);

		assert (swr_init (swr)>=0);

}

static void clear_convertor ()
{
	swr_close (swr);
	swr_free (&swr);
}

static VALUE
dj_each (VALUE arg, VALUE oparg, int argsc, const VALUE *args)
{
	const char *path = (RSTRING_PTR (arg));
	int ret;
	AVFormatContext *fmt=0;

	do {
		ret = avformat_open_input (&fmt, path, 0, 0);
		if (ret<0) break;
		ret = avformat_find_stream_info (fmt, 0);
		if (ret<0) break;
		do {
			AVCodecContext *decoder=0;
			AVCodec *dec=0;
			int si=0;
		ret = av_find_best_stream (fmt, AVMEDIA_TYPE_AUDIO, -1, -1, &dec, 0);
		if (ret<0) break;
		si = ret;
		decoder = avcodec_alloc_context3 (dec);
		if (!decoder) break;
		ret = avcodec_parameters_to_context (decoder,
				fmt->streams[si]->codecpar);
		ret=avcodec_open2 (decoder, dec, 0);
		if (ret<0) break;

		pthread_mutex_lock (&mmutex);
		init_convertor (encoder, decoder);
		pthread_mutex_unlock (&mmutex);

		while (1)
		{
			AVPacket pkt;

			ret=av_read_frame (fmt, &pkt);
			if (ret<0) break;
			do {
				if (pkt.stream_index!=si) break;
				ret = avcodec_send_packet (decoder, &pkt);
				if (ret<0) break;
					AVFrame *frame = av_frame_alloc ();
				while(1)
				{
					ret=avcodec_receive_frame (decoder, frame);
					if (ret<0) break;
					pthread_mutex_lock (&mmutex);
					decode_my_frame (frame);
					pthread_mutex_unlock (&mmutex);
				}
				av_frame_free (&frame);
			} while (0);
			av_packet_unref (&pkt);
		}
		} while (0);
		avformat_close_input (&fmt);
	} while (0);
	return(Qnil);
}

static int dj_cl_main (int argsc, char **args)
{
	VALUE ar;

	if (!args[0]) return(0);

	ar = rb_funcall (rb_cFile, rb_intern ("join"), 2,
			rb_str_new_cstr (args[1]), rb_str_new_cstr ("*"));

	ar = rb_funcall (rb_cDir, rb_intern ("[]"), 1, ar);

	while (1)
		rb_block_call (ar, rb_intern ("each"), 0, 0, dj_each, Qnil);
	return (0);
}

void dj_finalize ()
{
	pthread_mutex_destroy (&rbutex);
	pthread_mutex_destroy (&mmutex);
	ruby_finalize ();
}

int main (int argsc, char **args)
{
		dj_init ();
		dj_run_safe (dj_cl_main, argsc, args);
		dj_finalize ();
}
