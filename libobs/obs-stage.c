/******************************************************************************
    Copyright (C) 2025 by TechDevGroup

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
******************************************************************************/

#include "obs.h"
#include "obs-internal.h"

/*** Signals ***/

static const char *stage_signals[] = {
	"void destroy(ptr stage)",
	"void remove(ptr stage)",
	"void output_add(ptr stage, ptr output)",
	"void output_remove(ptr stage, ptr output)",
	"void output_start(ptr stage, ptr output)",
	"void output_stop(ptr stage, ptr output)",
	"void rename(ptr stage, string new_name, string prev_name)",
	NULL,
};

static inline void stage_dosignal(obs_stage_t *stage, const char *signal_obs,
				  const char *signal_stage)
{
	struct calldata data;
	uint8_t stack[128];

	calldata_init_fixed(&data, stack, sizeof(stack));
	calldata_set_ptr(&data, "stage", stage);
	if (signal_obs)
		signal_handler_signal(obs->signals, signal_obs, &data);
	if (signal_stage)
		signal_handler_signal(stage->context.signals, signal_stage,
				      &data);
}

static inline void stage_dosignal_output(obs_stage_t *stage, const char *signal,
					 obs_output_t *output)
{
	struct calldata data;
	uint8_t stack[128];

	calldata_init_fixed(&data, stack, sizeof(stack));
	calldata_set_ptr(&data, "stage", stage);
	calldata_set_ptr(&data, "output", output);

	signal_handler_signal(stage->context.signals, signal, &data);
}

/*** Reference Counting ***/

void obs_stage_release(obs_stage_t *stage)
{
	if (!obs && stage) {
		blog(LOG_WARNING,
		     "Tried to release a stage when the OBS core is shut down!");
		return;
	}

	if (!stage)
		return;

	obs_weak_stage_t *control = (obs_weak_stage_t *)stage->context.control;
	if (obs_ref_release(&control->ref)) {
		obs_stage_destroy(stage);
		obs_weak_stage_release(control);
	}
}

void obs_weak_stage_addref(obs_weak_stage_t *weak)
{
	if (!weak)
		return;

	obs_weak_ref_addref(&weak->ref);
}

void obs_weak_stage_release(obs_weak_stage_t *weak)
{
	if (!weak)
		return;

	if (obs_weak_ref_release(&weak->ref))
		bfree(weak);
}

obs_stage_t *obs_stage_get_ref(obs_stage_t *stage)
{
	if (!stage)
		return NULL;

	return obs_weak_stage_get_stage(
		(obs_weak_stage_t *)stage->context.control);
}

obs_weak_stage_t *obs_stage_get_weak_stage(obs_stage_t *stage)
{
	if (!stage)
		return NULL;

	obs_weak_stage_t *weak = (obs_weak_stage_t *)stage->context.control;
	obs_weak_stage_addref(weak);
	return weak;
}

obs_stage_t *obs_weak_stage_get_stage(obs_weak_stage_t *weak)
{
	if (!weak)
		return NULL;

	if (obs_weak_ref_get_ref(&weak->ref))
		return weak->stage;

	return NULL;
}

/*** Creation / Destruction ***/

static obs_stage_t *obs_stage_create_internal(const char *name, const char *uuid,
					      struct obs_video_info *ovi,
					      uint32_t flags, bool private)
{
	struct obs_stage *stage = bzalloc(sizeof(struct obs_stage));
	stage->flags = flags;

	if (!obs_context_data_init(&stage->context, OBS_OBJ_TYPE_INVALID, NULL,
				   name, uuid, NULL, private)) {
		bfree(stage);
		return NULL;
	}

	if (!signal_handler_add_array(stage->context.signals, stage_signals)) {
		obs_context_data_free(&stage->context);
		bfree(stage);
		return NULL;
	}

	if (pthread_mutex_init(&stage->outputs_mutex, NULL) != 0) {
		obs_context_data_free(&stage->context);
		bfree(stage);
		return NULL;
	}

	/* Create underlying canvas for this stage */
	uint32_t canvas_flags = 0;
	if (flags & OBS_STAGE_MIX_AUDIO)
		canvas_flags |= MIX_AUDIO;
	if (flags & OBS_STAGE_EPHEMERAL)
		canvas_flags |= EPHEMERAL;

	stage->canvas = obs_canvas_create_private(name, ovi, canvas_flags);
	if (!stage->canvas) {
		pthread_mutex_destroy(&stage->outputs_mutex);
		obs_context_data_free(&stage->context);
		bfree(stage);
		return NULL;
	}

	obs_context_init_control(&stage->context, stage,
				 (obs_destroy_cb)obs_stage_destroy);

	/* Add to global stage list */
	pthread_mutex_lock(&obs->data.stages_mutex);
	stage->next = obs->data.first_stage;
	stage->prev_next = &obs->data.first_stage;
	if (obs->data.first_stage)
		obs->data.first_stage->prev_next = &stage->next;
	obs->data.first_stage = stage;
	pthread_mutex_unlock(&obs->data.stages_mutex);

	if (!private)
		stage_dosignal(stage, "stage_create", NULL);

	blog(LOG_DEBUG, "%sstage '%s' created", private ? "private " : "",
	     stage->context.name);

	return stage;
}

obs_stage_t *obs_stage_create(const char *name, struct obs_video_info *ovi,
			      uint32_t flags)
{
	flags &= ~OBS_STAGE_MAIN;
	return obs_stage_create_internal(name, NULL, ovi, flags, false);
}

obs_stage_t *obs_stage_create_private(const char *name,
				      struct obs_video_info *ovi,
				      uint32_t flags)
{
	flags &= ~OBS_STAGE_MAIN;
	return obs_stage_create_internal(name, NULL, ovi, flags, true);
}

void obs_stage_destroy(obs_stage_t *stage)
{
	if (!stage)
		return;

	stage_dosignal(stage, "stage_destroy", "destroy");

	/* Stop and release all outputs */
	pthread_mutex_lock(&stage->outputs_mutex);
	for (size_t i = 0; i < stage->outputs.num; i++) {
		obs_output_t *output = stage->outputs.array[i];
		if (output) {
			if (obs_output_active(output))
				obs_output_stop(output);
			obs_output_release(output);
		}
	}
	da_free(stage->outputs);
	pthread_mutex_unlock(&stage->outputs_mutex);

	/* Release canvas */
	if (stage->canvas) {
		obs_canvas_release(stage->canvas);
		stage->canvas = NULL;
	}

	/* Remove from global stage list */
	pthread_mutex_lock(&obs->data.stages_mutex);
	if (stage->next)
		stage->next->prev_next = stage->prev_next;
	*stage->prev_next = stage->next;
	pthread_mutex_unlock(&obs->data.stages_mutex);

	blog(LOG_DEBUG, "%sstage '%s' destroyed",
	     stage->context.private ? "private " : "", stage->context.name);

	pthread_mutex_destroy(&stage->outputs_mutex);
	obs_context_data_free(&stage->context);
	bfree(stage);
}

/*** Output Management ***/

bool obs_stage_add_output(obs_stage_t *stage, obs_output_t *output)
{
	if (!stage || !output)
		return false;

	pthread_mutex_lock(&stage->outputs_mutex);

	/* Check if output is already added */
	for (size_t i = 0; i < stage->outputs.num; i++) {
		if (stage->outputs.array[i] == output) {
			pthread_mutex_unlock(&stage->outputs_mutex);
			return false;
		}
	}

	obs_output_t *ref = obs_output_get_ref(output);
	if (!ref) {
		pthread_mutex_unlock(&stage->outputs_mutex);
		return false;
	}

	da_push_back(stage->outputs, &ref);
	pthread_mutex_unlock(&stage->outputs_mutex);

	stage_dosignal_output(stage, "output_add", output);

	blog(LOG_DEBUG, "stage '%s': added output '%s'", stage->context.name,
	     obs_output_get_name(output));

	return true;
}

bool obs_stage_remove_output(obs_stage_t *stage, obs_output_t *output)
{
	if (!stage || !output)
		return false;

	pthread_mutex_lock(&stage->outputs_mutex);

	for (size_t i = 0; i < stage->outputs.num; i++) {
		if (stage->outputs.array[i] == output) {
			if (obs_output_active(output))
				obs_output_stop(output);

			stage_dosignal_output(stage, "output_remove", output);

			obs_output_release(output);
			da_erase(stage->outputs, i);
			pthread_mutex_unlock(&stage->outputs_mutex);

			blog(LOG_DEBUG, "stage '%s': removed output '%s'",
			     stage->context.name, obs_output_get_name(output));
			return true;
		}
	}

	pthread_mutex_unlock(&stage->outputs_mutex);
	return false;
}

size_t obs_stage_get_output_count(const obs_stage_t *stage)
{
	if (!stage)
		return 0;

	return stage->outputs.num;
}

obs_output_t *obs_stage_get_output(const obs_stage_t *stage, size_t idx)
{
	if (!stage || idx >= stage->outputs.num)
		return NULL;

	return stage->outputs.array[idx];
}

/*** Output Control ***/

bool obs_stage_start_output(obs_stage_t *stage, size_t idx)
{
	if (!stage || idx >= stage->outputs.num)
		return false;

	obs_output_t *output = stage->outputs.array[idx];
	if (!output)
		return false;

	bool success = obs_output_start(output);
	if (success)
		stage_dosignal_output(stage, "output_start", output);

	return success;
}

void obs_stage_stop_output(obs_stage_t *stage, size_t idx, bool force)
{
	if (!stage || idx >= stage->outputs.num)
		return;

	obs_output_t *output = stage->outputs.array[idx];
	if (!output)
		return;

	obs_output_stop(output);
	stage_dosignal_output(stage, "output_stop", output);

	UNUSED_PARAMETER(force);
}

void obs_stage_start_all_outputs(obs_stage_t *stage)
{
	if (!stage)
		return;

	pthread_mutex_lock(&stage->outputs_mutex);
	for (size_t i = 0; i < stage->outputs.num; i++) {
		obs_output_t *output = stage->outputs.array[i];
		if (output && !obs_output_active(output)) {
			if (obs_output_start(output))
				stage_dosignal_output(stage, "output_start",
						      output);
		}
	}
	pthread_mutex_unlock(&stage->outputs_mutex);
}

void obs_stage_stop_all_outputs(obs_stage_t *stage, bool force)
{
	if (!stage)
		return;

	pthread_mutex_lock(&stage->outputs_mutex);
	for (size_t i = 0; i < stage->outputs.num; i++) {
		obs_output_t *output = stage->outputs.array[i];
		if (output && obs_output_active(output)) {
			obs_output_stop(output);
			stage_dosignal_output(stage, "output_stop", output);
		}
	}
	pthread_mutex_unlock(&stage->outputs_mutex);

	UNUSED_PARAMETER(force);
}

bool obs_stage_any_output_active(const obs_stage_t *stage)
{
	if (!stage)
		return false;

	for (size_t i = 0; i < stage->outputs.num; i++) {
		if (obs_output_active(stage->outputs.array[i]))
			return true;
	}
	return false;
}

/*** Canvas Access ***/

obs_canvas_t *obs_stage_get_canvas(obs_stage_t *stage)
{
	if (!stage)
		return NULL;
	return stage->canvas;
}

video_t *obs_stage_get_video(obs_stage_t *stage)
{
	if (!stage || !stage->canvas)
		return NULL;
	return obs_canvas_get_video(stage->canvas);
}

bool obs_stage_get_video_info(obs_stage_t *stage, struct obs_video_info *ovi)
{
	if (!stage || !stage->canvas)
		return false;
	return obs_canvas_get_video_info(stage->canvas, ovi);
}

/*** Scene Management ***/

void obs_stage_set_scene(obs_stage_t *stage, obs_scene_t *scene)
{
	if (!stage || !stage->canvas)
		return;

	obs_source_t *source = obs_scene_get_source(scene);
	obs_canvas_set_channel(stage->canvas, 0, source);
}

obs_source_t *obs_stage_get_scene_source(obs_stage_t *stage)
{
	if (!stage || !stage->canvas)
		return NULL;

	return obs_canvas_get_channel(stage->canvas, 0);
}

/*** Properties ***/

const char *obs_stage_get_name(const obs_stage_t *stage)
{
	if (!stage)
		return NULL;
	return stage->context.name;
}

void obs_stage_set_name(obs_stage_t *stage, const char *name)
{
	if (!stage || !name || !*name)
		return;

	if (stage->flags & OBS_STAGE_MAIN)
		return;

	if (strcmp(name, stage->context.name) == 0)
		return;

	char *prev_name = bstrdup(stage->context.name);
	obs_context_data_setname(&stage->context, name);

	/* Also rename the canvas */
	if (stage->canvas)
		obs_canvas_set_name(stage->canvas, name);

	struct calldata data;
	calldata_init(&data);
	calldata_set_ptr(&data, "stage", stage);
	calldata_set_string(&data, "new_name", stage->context.name);
	calldata_set_string(&data, "prev_name", prev_name);
	signal_handler_signal(stage->context.signals, "rename", &data);

	if (!stage->context.private)
		signal_handler_signal(obs->signals, "stage_rename", &data);

	calldata_free(&data);
	bfree(prev_name);
}

uint32_t obs_stage_get_flags(const obs_stage_t *stage)
{
	if (!stage)
		return 0;
	return stage->flags;
}

signal_handler_t *obs_stage_get_signal_handler(obs_stage_t *stage)
{
	if (!stage)
		return NULL;
	return stage->context.signals;
}

/*** Enumeration ***/

void obs_enum_stages(bool (*enum_proc)(void *, obs_stage_t *), void *param)
{
	if (!enum_proc)
		return;

	pthread_mutex_lock(&obs->data.stages_mutex);

	obs_stage_t *stage = obs->data.first_stage;
	while (stage) {
		obs_stage_t *next = stage->next;
		if (!enum_proc(param, stage))
			break;
		stage = next;
	}

	pthread_mutex_unlock(&obs->data.stages_mutex);
}

obs_stage_t *obs_get_stage_by_name(const char *name)
{
	if (!name || !*name)
		return NULL;

	pthread_mutex_lock(&obs->data.stages_mutex);

	obs_stage_t *stage = obs->data.first_stage;
	while (stage) {
		if (strcmp(stage->context.name, name) == 0) {
			obs_stage_t *ref = obs_stage_get_ref(stage);
			pthread_mutex_unlock(&obs->data.stages_mutex);
			return ref;
		}
		stage = stage->next;
	}

	pthread_mutex_unlock(&obs->data.stages_mutex);
	return NULL;
}

/*** Saving / Loading ***/

obs_data_t *obs_save_stage(obs_stage_t *stage)
{
	if (!stage || (stage->flags & OBS_STAGE_EPHEMERAL))
		return NULL;

	obs_data_t *stage_data = obs_data_create();

	obs_data_set_string(stage_data, "name", stage->context.name);
	obs_data_set_int(stage_data, "flags", stage->flags);

	/* Save video info */
	struct obs_video_info ovi;
	if (obs_stage_get_video_info(stage, &ovi)) {
		obs_data_set_int(stage_data, "base_width", ovi.base_width);
		obs_data_set_int(stage_data, "base_height", ovi.base_height);
		obs_data_set_int(stage_data, "output_width", ovi.output_width);
		obs_data_set_int(stage_data, "output_height", ovi.output_height);
		obs_data_set_int(stage_data, "fps_num", ovi.fps_num);
		obs_data_set_int(stage_data, "fps_den", ovi.fps_den);
	}

	return stage_data;
}

obs_stage_t *obs_load_stage(obs_data_t *data)
{
	if (!data)
		return NULL;

	const char *name = obs_data_get_string(data, "name");
	uint32_t flags = (uint32_t)obs_data_get_int(data, "flags");

	struct obs_video_info ovi = {0};
	ovi.base_width = (uint32_t)obs_data_get_int(data, "base_width");
	ovi.base_height = (uint32_t)obs_data_get_int(data, "base_height");
	ovi.output_width = (uint32_t)obs_data_get_int(data, "output_width");
	ovi.output_height = (uint32_t)obs_data_get_int(data, "output_height");
	ovi.fps_num = (uint32_t)obs_data_get_int(data, "fps_num");
	ovi.fps_den = (uint32_t)obs_data_get_int(data, "fps_den");

	flags &= ~OBS_STAGE_MAIN;
	return obs_stage_create_internal(name, NULL, &ovi, flags, false);
}

/*** Internal ***/

void obs_free_stages(void)
{
	pthread_mutex_lock(&obs->data.stages_mutex);

	obs_stage_t *stage = obs->data.first_stage;
	while (stage) {
		obs_stage_t *next = stage->next;
		obs_stage_destroy(stage);
		stage = next;
	}
	obs->data.first_stage = NULL;

	pthread_mutex_unlock(&obs->data.stages_mutex);
}
