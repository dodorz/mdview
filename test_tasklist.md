# Task List Test

## Basic Task Lists

- [ ] Unchecked task 1
- [x] Completed task 1
- [ ] Unchecked task 2
- [X] Completed task 2 (uppercase X)

## Mixed with Regular Lists

- Regular list item
- [ ] Task list item
- Another regular item
- [x] Completed task

## Nested Task Lists

- [ ] Parent task
  - [ ] Child task 1
  - [x] Child task 2 (completed)
  - [ ] Child task 3
- [x] Another parent task
  - [x] All children done

## Task Lists with Formatting

- [ ] Task with **bold** text
- [x] Task with *italic* text
- [ ] Task with `inline code`
- [x] Task with [link](https://example.com)

## Edge Cases

-[ ] No space after dash (should not render as task)
- [] No space in checkbox (should not render as task)
- [ x ] Spaces in checkbox (should not render as task)
