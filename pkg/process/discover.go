package process

import (
	"github.com/keyval-dev/opentelemetry-go-instrumentation/pkg/errors"
	"github.com/keyval-dev/opentelemetry-go-instrumentation/pkg/log"
	"io"
	"io/ioutil"
	"os"
	"path"
	"strconv"
	"strings"
	"time"
)

const (
	initExeName = "/odigos/init"
)

type processAnalyzer struct {
	done          chan bool
	pidTickerChan <-chan time.Time
}

func NewAnalyzer() *processAnalyzer {
	return &processAnalyzer{
		done:          make(chan bool, 1),
		pidTickerChan: time.NewTicker(2 * time.Second).C,
	}
}

func (a *processAnalyzer) DiscoverProcessID(target *TargetArgs) (int, error) {
	for {
		select {
		case <-a.done:
			log.Logger.V(0).Info("stopping process id discovery due to kill signal")
			return 0, errors.ErrInterrupted
		case <-a.pidTickerChan:
			pid, err := a.findProcessID(target)
			if err != nil {
				if err == errors.ErrProcessNotFound {
					log.Logger.V(0).Info("process not found yet, trying again soon", "exe_path", target.ExePath)
				} else {
					log.Logger.Error(err, "error while searching for process", "exe_path", target.ExePath)
				}
			} else {
				log.Logger.V(0).Info("found process", "pid", pid)
				return pid, nil
			}
		}
	}
}

func (a *processAnalyzer) findProcessID(target *TargetArgs) (int, error) {
	proc, err := os.Open("/proc")
	if err != nil {
		return 0, err
	}

	for {
		dirs, err := proc.Readdir(15)
		if err == io.EOF {
			break
		}
		if err != nil {
			return 0, err
		}

		for _, di := range dirs {
			if !di.IsDir() {
				continue
			}

			dname := di.Name()
			if dname[0] < '0' || dname[0] > '9' {
				continue
			}

			pid, err := strconv.Atoi(dname)
			if err != nil {
				return 0, err
			}

			exeName, err := os.Readlink(path.Join("/proc", dname, "exe"))
			if err != nil {
				// Read link may fail if target process runs not as root
				cmdLine, err := ioutil.ReadFile(path.Join("/proc", dname, "cmdline"))
				if err != nil {
					return 0, err
				}

				if !strings.Contains(string(cmdLine), initExeName) && strings.Contains(string(cmdLine), target.ExePath) {
					return pid, nil
				}
			} else if exeName == target.ExePath {
				return pid, nil
			}
		}
	}

	return 0, errors.ErrProcessNotFound
}

func (a *processAnalyzer) Close() {
	a.done <- true
}
