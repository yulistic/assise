#!/bin/bash
(
	# Set project root.
        cd "/home/jongyul/assise"

        SESSION_NAME="assise"

        # Check if tmux session exists, and create if not.
        tmux has-session -t $SESSION_NAME 2>/dev/null

        if [ $? != 0 ]; then
                # Create session.
                tmux new-session -d -s $SESSION_NAME

          # pane 0: setup machine && go to assise directory.
          tmux send-keys -t $SESSION_NAME C-m
          tmux send-keys "cd kernfs/tests; echo 'kernfs runs here.'" C-m

          # pane bottom left:
          tmux split-window -h
          tmux send-keys "tty" C-m

          # pane bottom right:
          tmux split-window -v
          tmux send-keys "tty" C-m

          # pane top right:
          tmux select-pane -t 0
          tmux split-window -v
          tmux send-keys "cd bench/micro; echo 'microbench runs here. Use run_tput_mux.sh script to run experiments.'" C-m

          # Arrange panes evenly.
          tmux select-layout tiled
        fi

        # Attach to session.
        tmux attach -t $SESSION_NAME
)
