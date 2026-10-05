"""Inject the production Broker service into the production session SDK."""

import os
import uuid

from ue_node_nexus_mcp.instances.errors import InstanceError


class LocalBroker:
    def __init__(self, service, workspace):
        self.service = service
        self.root = service.root
        self.workspace = str(workspace)
        self.session_id = uuid.uuid4().hex
        self.epoch = service.epoch
        self.policy = service.policy_dict()
        self.features = frozenset()
        self.registered = False

    def connect(self):
        if self.registered:
            return
        self.registered = True
        result = self.send("register", dict(client_session_id=self.session_id, workspace=self.workspace,
                                            process_created=self.service.platform.inspect(os.getpid())["process_created"]))
        self.features = frozenset(result.get("features", ()))

    def supports(self, feature):
        self.connect()
        return feature in self.features

    def call(self, action, payload=None):
        self.connect()
        return self.send(action, payload)

    def send(self, action, payload=None):
        response = self.service.dispatch(dict(protocol=1, action=action, payload=payload or dict(),
                                              client_session_id=self.session_id), os.getpid())
        self.epoch = self.service.epoch
        if not response["ok"]:
            error = response["error"]
            raise InstanceError(error["code"], error["message"], error.get("details"))
        return response["data"]

    def close(self):
        if self.registered:
            self.call("client_end")
            self.registered = False
