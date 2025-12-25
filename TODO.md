- Finish fixing the compile errors in `src/event.c`.
- Build the project and make sure there are no compile errors.
- Run the tests to make sure everything is working as expected.

## `rg` commands to find strings to be replaced in `src/event.c`

`rg "hash_map_get\(&s->buckets" src/event.c`
`rg "ptr_to_handle" src/event.c`