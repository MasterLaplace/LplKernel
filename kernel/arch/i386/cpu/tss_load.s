.section .text
.global task_state_segment_load

.type task_state_segment_load, @function
task_state_segment_load:
    movl 4(%esp), %eax
    ltr %ax
    ret
