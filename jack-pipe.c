#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#define MIN( a, b ) ( ( a < b ) ? a : b )

#include <jack/jack.h>
#include <sndfile.h>

jack_port_t *input_port;
jack_port_t *output_port;
jack_client_t *client;
const char *input_file_name, *input_port_name, *output_port_name, *output_file_name;
float *input_file_data, *output_file_data;
long unsigned input_file_frames, output_file_frames;
long unsigned input_offset = 0, output_offset = 0;
long unsigned samplerate;

void
write_and_exit()
{
  SNDFILE	*outfile;
  SF_INFO	sfinfo;
  sfinfo.channels = 1;
  sfinfo.samplerate = samplerate;
  sfinfo.format = SF_FORMAT_WAV | SF_FORMAT_PCM_16;
  // jack_client_close (client);
  if (! (outfile = sf_open (output_file_name, SFM_WRITE, &sfinfo))) {
    fprintf(stderr, "libsndfile error: %s %s\n", sf_strerror (NULL), output_file_name);
    exit (1);
  }
  sf_write_float (outfile, output_file_data, output_file_frames);
  sf_close (outfile);
  exit (0);
}

/**
 * The process callback for this JACK application is called in a
 * special realtime thread once for each audio cycle.
 *
 * This client does nothing more than copy data from its input
 * port to its output port. It will exit when stopped by
 * the user (e.g. using Ctrl-C on a unix-ish operating system)
 */
int
process (jack_nframes_t nframes, void *arg)
{
	jack_default_audio_sample_t *in, *out;

	in = jack_port_get_buffer (input_port, nframes);
	out = jack_port_get_buffer (output_port, nframes);

  // copy input_file_data to
  long frames_to_write = MIN(nframes, input_file_frames - input_offset);
	memcpy (out, input_file_data + input_offset,
		sizeof (jack_default_audio_sample_t) * frames_to_write);
  memset(out, 0, sizeof (jack_default_audio_sample_t));
  input_offset += frames_to_write;

  long frames_to_read = MIN(nframes, output_file_frames - output_offset);
  memcpy (output_file_data + output_offset, in, sizeof (float) * frames_to_read);
  output_offset += frames_to_read;

	return 0;
}

/**
 * JACK calls this shutdown_callback if the server ever shuts down or
 * decides to disconnect the client.
 */
void
jack_shutdown (void *arg)
{
	exit (1);
}

int
main (int argc, char *argv[])
{
	const char **ports;
	const char *client_name = "jack-pipe";
	const char *server_name = NULL;
	jack_options_t options = JackNullOption;
	jack_status_t status;
  SF_INFO	sfinfo;
  SNDFILE	*infile;

  if (argc != 6) {
    fprintf (stderr, "Usage: %s input.wav input_port output_port output.wav samples\n", argv[0]);
    exit (1);
  }

  input_file_name = argv[1];
  input_port_name = argv[2];
  output_port_name = argv[3];
  output_file_name = argv[4];
  output_file_frames = atoi (argv[5]);

  if (! (infile = sf_open (input_file_name, SFM_READ, &sfinfo))) {
    fprintf(stderr, "libsndfile error: %s\n", sf_strerror (NULL));
    exit (1);
  }

  if (sfinfo.channels != 1) {
    fprintf (stderr, "Invalid number of channels %i only one is supported\n", sfinfo.channels);
    exit (1);
  }

  input_file_frames = sfinfo.frames;
  samplerate = sfinfo.samplerate;
  input_file_data = malloc (sizeof(float) * sfinfo.frames);
  sf_read_float (infile, input_file_data, sfinfo.frames);
  output_file_data = malloc (sizeof(float) * output_file_frames);
  sf_close (infile);

	/* open a client connection to the JACK server */

	client = jack_client_open (client_name, options, &status, server_name);
	if (client == NULL) {
		fprintf (stderr, "jack_client_open() failed, "
			 "status = 0x%2.0x\n", status);
		if (status & JackServerFailed) {
			fprintf (stderr, "Unable to connect to JACK server\n");
		}
		exit (1);
	}
	if (status & JackServerStarted) {
		fprintf (stderr, "JACK server started\n");
	}
	if (status & JackNameNotUnique) {
		client_name = jack_get_client_name(client);
		fprintf (stderr, "unique name `%s' assigned\n", client_name);
	}

	/* tell the JACK server to call `process()' whenever
	   there is work to be done.
	*/

	jack_set_process_callback (client, process, 0);

	/* tell the JACK server to call `jack_shutdown()' if
	   it ever shuts down, either entirely, or if it
	   just decides to stop calling us.
	*/

	jack_on_shutdown (client, jack_shutdown, 0);

	/* display the current sample rate.
	 */

  samplerate = jack_get_sample_rate (client);

  if (samplerate != sfinfo.samplerate) {
    fprintf(stderr,
      "\x1b[1;31mWARNING: samplerate mismatch jack: %lu hz, %s: %i hz\x1B[0m\n",
     samplerate, input_file_name, sfinfo.samplerate);
  }

	/* create two ports */

	input_port = jack_port_register (client, "input",
					 JACK_DEFAULT_AUDIO_TYPE,
					 JackPortIsInput, 0);
	output_port = jack_port_register (client, "output",
					  JACK_DEFAULT_AUDIO_TYPE,
					  JackPortIsOutput, 0);

	if ((input_port == NULL) || (output_port == NULL)) {
		fprintf(stderr, "no more JACK ports available\n");
		exit (1);
	}

	/* Tell the JACK server that we are ready to roll.  Our
	 * process() callback will start running now. */

	if (jack_activate (client)) {
		fprintf (stderr, "cannot activate client");
		exit (1);
	}

	/* Connect the ports.  You can't do this before the client is
	 * activated, because we can't make connections to clients
	 * that aren't running.  Note the confusing (but necessary)
	 * orientation of the driver backend ports: playback ports are
	 * "input" to the backend, and capture ports are "output" from
	 * it.
	 */

	ports = jack_get_ports (client, NULL, NULL,
				JackPortIsPhysical|JackPortIsOutput);
	if (ports == NULL) {
		fprintf(stderr, "no physical capture ports\n");
		exit (1);
	}

	if (jack_connect (client, ports[0], jack_port_name (input_port))) {
		fprintf (stderr, "cannot connect input ports\n");
	}

	free (ports);

	ports = jack_get_ports (client, NULL, NULL,
				JackPortIsPhysical|JackPortIsInput);
	if (ports == NULL) {
		fprintf(stderr, "no physical playback ports\n");
		exit (1);
	}

	if (jack_connect (client, jack_port_name (output_port), ports[0])) {
		fprintf (stderr, "cannot connect output ports\n");
	}

	free (ports);

	/* keep running until stopped by the user */

	while (output_offset < output_file_frames) {
    usleep (10);
  }

	/* this is never reached but if the program
	   had some other way to exit besides being killed,
	   they would be important to call.
	*/

	write_and_exit();
}
