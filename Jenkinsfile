pipeline {
  agent any
  stages {
    stage('Build') {
      steps {
        script {
          OS-Builds/ubuntu-build
        }

      }
    }

    stage('done') {
      steps {
        setGitHubPullRequestStatus(context: 'Built', message: 'Done')
      }
    }

  }
}